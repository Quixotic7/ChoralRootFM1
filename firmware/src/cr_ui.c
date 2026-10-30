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
 * switches to that one, a tap is still the on/off action. OPT (the knob shift) is momentary; SAVE held is no layer:
 * the global save (cu_gsave; GLO + SAVE held: the revert, cu_revert). B3 = LOCK, the chord block's latch mode (cu_mod_press: a virtual hold of the latched keys).
 * Output: events for the engine (cr_out.c cr_post, drained by the audio ISR), Felucca's part parameters (sounds,
 * sends, levels: written as Felucca's UI writes them), fm1_led / fm1_led_dim, and one cr_screen_t per frame for
 * cr_draw.c.
 *
 * Sounds (docs/PRESETS.md): PRESETS / ALGORITHM turn through the pool of the part's engine (cr_bank.c: INIT, the
 * engine's factory presets, each replaced by the user's record bound to it, the presets the user added; the records are
 * upreset.c's 32 slots); OPT + PRESETS changes the chord part's engine; EDIT opens the sound pages (cr_pages.c), EDIT
 * held the engine picker, SAVE the save dialog (Overwrite / Save as new, then the naming screen, cr_name.c).
 * The looper (cr_loop.c, docs/LOOPER.md): LOOP / REC / METRO and their layers, the loop slots in flash (written by
 * the global save and by leaving a slot), the loop editor (EDIT in the PLAY menu),
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
enum { L_NONE, L_KEY, L_PERF, L_FX, L_BASS, L_EDIT, L_RSV6, L_METRO, L_LOOP, L_VIEW, L_LOCK, L_N };   /* L_VIEW: HOME
                                                   * held; L_LOCK: the key B3 held (no button: NB); L_RSV6: retired
                                                   * (until 0.17 SAVE held: the loop slots' Save / Load / Delete map;
                                                   * SAVE held is the global save now, cu_gsave: the numbers stay) */
static const uint8_t L_BTN[L_N] = {NB, BT_KEY, BT_PERF, BT_FX, BT_BASS, BT_EDIT, NB, BT_METRO, BT_LOOP, BT_HOME, NB};
                                                  /* (L_LOCK's: B3 in Classic, LFO in FM-1: cu_lbtn) */

/* the chord block: firmware key k (0 = F3) -> cr_mod_t, 0xFF = none. Two layouts (Options > Chord Keys,
 * docs/CHORD-LAYOUT.md): Classic, the Orchid's (B3 LOCK, C4 9, C#4 SUS, the roots from D4), and FM-1: F#3 G#3 A#3
 * the types DIM MIN MAJ, SUS the pair G#3 + A#3 (cu_tk_*: no key of its own), F3 G3 A3 B3 the extensions 6 m7 M7 9,
 * the roots from C4 (20 of them); LOCK on LFO there */
static const uint8_t CU_MOD_OF_KEY[9] = {CR_MOD_6, CR_MOD_DIM, CR_MOD_M7, CR_MOD_MIN, CR_MOD_MAJ7, CR_MOD_MAJ, 0xFFu,
                                         CR_MOD_9, CR_MOD_SUS};
static const uint8_t CU_MOD_OF_KEY_FM1[9] = {CR_MOD_6, CR_MOD_DIM, CR_MOD_M7, CR_MOD_MIN, CR_MOD_MAJ7, CR_MOD_MAJ,
                                             CR_MOD_9, 0xFFu, 0xFFu};
#define CU_ROOT0 9u                               /* D4: Classic's first root key */
#define CU_ROOT0_FM1 7u                           /* C4: FM-1's */
#define CU_PICK0 9u                               /* D4: where the layers' pick maps start, in both layouts (the
                                                   * owner, 2026-10-16: the loop slots, naming, the KEY layer's tonic
                                                   * stay where they are) */
#define CU_NKEY 27u
#define CU_TK_MASK ((1u << 1) | (1u << 3) | (1u << 5))   /* FM-1's type keys F#3 G#3 A#3 (key bits) */
static int cu_black(uint32_t k) { return (int)((0x54Au >> ((k + 5u) % 12u)) & 1u); }
/* a root key's place among the white keys of the pick maps (D4 = 0 .. G5 = 10), -1 for a black one */
static int32_t cu_white_idx(uint32_t k)
{
    uint32_t i, n = 0;
    if (k < CU_PICK0 || k >= CU_NKEY || cu_black(k))
        return -1;
    for (i = CU_PICK0; i < k; i++)
        n += !cu_black(i);
    return (int32_t)n;
}
static uint32_t cu_white_key(uint32_t idx)        /* the inverse: white root idx -> key, CU_NKEY = none */
{
    uint32_t k;
    for (k = CU_PICK0; k < CU_NKEY; k++)
        if (cu_white_idx(k) == (int32_t)idx)
            return k;
    return CU_NKEY;
}

/* the panel's sizes: the LED ids (cr_leds: buttons, then the note keys from CU_LK(0)), the calibration's buttons
 * and encoders */
#if BOARD_SMK37
#define CU_LK(k) (24u + (k))                       /* SMK-37: the buttons' ids go up to 17 (smk_input.h) */
#define CU_LED_IDS 64u
#define CU_NBTN FM1_NBTN
#define CU_BTN_MASK 0x3FFFFu
#define CU_ENC0 1u                                 /* (K1 is MASTER, smk_input.h: never a role) */
#define CU_NENC FM1_NENC
#define CU_TOUCH_MAX 1                             /* Options > Key Touch: Fixed, Dynamic (the keys' two contacts:
                                                    * smk_input.h key_vel); no pressure, so no aftertouch modes */
#define CU_VCURVE_MAX 0                            /* Velocity Curve, Aftertouch Sens: the FM-1's sensing (cr_sense.c) */
#define CU_ATSENS_MAX 0
#define CU_K1 "K5"                                 /* on-screen knob names: KNOB 1..4 are K5..K8 here (K1 is MASTER) */
#define CU_K2 "K6"
#define CU_K3 "K7"
#define CU_K4 "K8"
/* The SMK-37's extras (SMK37.md). The keybed plays: all 37 keys are roots (the chord block's nine keys too,
 * CU_FN_KEYS 0), and the chord block's keys are on the pads: the top row's left half the chord types (DIM MIN MAJ
 * SUS), the bottom row's the extensions (6 m7 M7 9), then LOCK. A pad is its key: held while it is held (cu_pheld, the
 * FM-1's cu_mod_press / cu_mod_release and LOCK's B3 rules), so combinations, LOCK's latch and the KEY layer's scale
 * pick work as on the FM-1. The other seven pads pick the performance mode (CU_PERF): the free-timing ones on the top
 * row (Strum, Strum 2 Octaves, Slop, Harp), the tempo-synced ones on the bottom row (Arpeggiate, Arp 2 Octaves,
 * Pattern). Every pad is the UI's (smk_pad_map all -1: smk_pad_take); hardware pads 0..7 are the top row, 8..15 the
 * bottom row */
#define CU_FN_KEYS 0                               /* the chord block's keys are not function keys here */
static const int8_t CU_PAD_FN[16] = {1, 3, 5, 8, -1, -1, -1, -1, 0, 2, 4, 7, 6, -1, -1, -1};   /* chord block key */
static const int8_t CU_PAD_PERF[16] = {-1, -1, -1, -1, 0, 1, 2, 6, -1, -1, -1, -1, -1, 3, 4, 5};   /* CU_PERF entry */
static const int8_t CU_PAD_NOTE[16] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1};
static uint16_t cu_pheld;                          /* bit k: the pad of chord block key k is down */
static uint16_t cu_ppick;                          /* .. pressed as the KEY layer's scale pick */
static uint8_t cu_xnote[10];                       /* the note an outer key started + 1 (C3..E3, G#5..C6) */
static uint16_t cu_xheld;                          /* the outer keys down */
static uint8_t cu_fbank, cu_fbank_new = 1;         /* FADER BANK: 0 the chord part's sends, 1 the bass part's */
#else
#define CU_LK(k) (14u + (k))
#define CU_LED_IDS 41u
#define CU_NBTN 14u
#define CU_BTN_MASK 0x3FFFu
#define CU_ENC0 0u
#define CU_NENC 7u
#define CU_TOUCH_MAX 3
#define CU_VCURVE_MAX 6
#define CU_ATSENS_MAX 2
#define CU_K1 "K1"
#define CU_K2 "K2"
#define CU_K3 "K3"
#define CU_K4 "K4"
#define CU_FN_KEYS 1                               /* the chord block's keys: chord functions (and B3 LOCK) */
#endif

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
static const char *const CU_SNAP[3] = {"Off", "Scale", "Chord"};   /* the KEY layer's Snap (cr_keysnap_t) */
static const char *const CU_NUMS[3] = {"Off", "On", "Solf\350ge"};   /* .. its Numerals (cs.numerals; \350: e grave,
                                                    * CRB's extra) */
static const char *const CU_NUMS_TR[3] = {"off", "on", "solfege"};

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
/* FM TONE's Drive is its preset's (below, at cu_fx_apply): every load that changes a part's engine or loads an FM
 * TONE sound says so BEFORE it writes eng_req: engine e is coming, with Drive d if e is FM TONE */
static void cu_drive_load(track_t *t, uint32_t e, int32_t d);
static int32_t cu_preset_drive(const preset_t *pr) { return pr->fx[0] ? (int32_t)pr->fx[0] - 1 : 0; }   /* core.h FX() */

/* engine e's preset pi into part t, as ui.c set_engine_of + apply_preset_to: the sound only (not its sends: they are
 * the part's, cu_kept; an FM TONE preset's Drive is loaded: cu_drive_load); the part's
 * notes are released (panic_req: cr_out.c events_block, the audio side). bass: P_VOICE MONO, else POLY (a chord on a
 * mono preset) */
static void cu_load(track_t *t, uint32_t e, uint32_t pi, int bass)
{
    const engine_t *en = ENGINES[e % NENGINES];
    const preset_t *pr;
    uint32_t i;
    if (!en->npresets)
        return;
    pi = cu_preset_orig(en, pi % en->npresets);
    pr = &en->presets[pi];
    fm1_irq_off();
    cu_drive_load(t, e % NENGINES, cu_preset_drive(pr));
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
 * the VA's init patch under them, eng_va.c va_track_loaded); a sound of another retired engine (PHASE 2, LOFI 3, VOICE 5,
 * TRIO 6, NOISE 11, PHYS 9, retired in 0.18: a 0.17 slot, a restored bank; SAMPLE 4, GRAIN 8, DRUM 10: a Felucca slot; engines.c
 * ENG_GONE) loads as the VA's init sound, a stand-in: the six of 0.18 keep the record's sends and its envelope's ATK /
 * REL on the VA's macros of that name (cu_analog_va's mapping; their other macros have no VA meaning). All under the
 * slot's name, with a message (and a trace line on the emulator) */
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
        cu_drive_load(t, ENGI_FM6, 0);
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
        cu_drive_load(t, ENGI_VA, 0);
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
        const char *was = r->engine == 2u ? "PHASE" : r->engine == 3u ? "LOFI" : r->engine == 5u ? "VOICE" :
                          r->engine == 6u ? "TRIO" : r->engine == 11u ? "NOISE" : r->engine == ENGI_PHYS ? "PHYS" : 0;
        int carry = was && ((ENG_RETIRED_018 >> r->engine) & 1u) && CB_RETIRED_POOL == ENGI_VA;
        char msg[24];                             /* (cu.msg.text holds 23 characters) */
#if defined(CR_TRACE) && CR_TRACE
        printf("load: slot %u engine %u%s%s%s retired -> INIT on %s%s\n", (unsigned)k + 1u, (unsigned)r->engine,
               was ? " (" : "", was ? was : "", was ? ")" : "", en->name, carry ? ", its ATK, REL" : "");
#endif
        fm1_irq_off();
        cu_drive_load(t, CB_RETIRED_POOL, 0);
        t->eng_req = CB_RETIRED_POOL;
        for (i = 0; i < P_E0; i++)
            if (!cu_kept(i))
                t->p[i] = TP[i].def;
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = en->edit[i].def;
        if (carry) {                              /* a stand-in for a sound of the five: its ATK / REL (its sends
                                                   * until the sends became the part's) */
            t->p[P_E0 + 6] = (int16_t)clamp(v[P_ATK], en->edit[6].min, en->edit[6].max);
            t->p[P_E0 + 7] = (int16_t)clamp(v[P_REL], en->edit[7].min, en->edit[7].max);
        }
        t->p[P_VOICE] = bass ? V_MONO : V_POLY;
        t->preset = 0;
        fm1_irq_on();
        fm6_track_loaded(t);
        if (was && carry) {                       /* "PHASE retired: stand-in" */
            str_cpy(msg, was, 6);
            str_cpy(msg + str_len(msg), " retired: stand-in", 19);
        }
        cu_message(was && carry ? msg : "engine retired: init sound", CR_COL_WHITE);
    } else {
        fm1_irq_off();
        cu_drive_load(t, r->engine, v[P_DIST]);   /* (an FM TONE record: the Drive it was saved with) */
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
    cu_drive_load(t, e % NENGINES, 0);            /* (FM TONE's INIT: Drive 0, its AMP page's default) */
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

/* FX (docs/FX.md): four effects on nine pages (SELECT: Sends, Reverb 1, Reverb 2, Chorus, Delay 1, Delay 2, Drive,
 * Filter: the master filter of 0.22, cr_fx.c xq_block, whose four parameters are xfp[] too; Routing: GitHub issue
 * 21's signal flow, cr_fx.c XP_RMODE (Parallel / Chain) and XP_ORDER (the four blocks' order), xfp[] as well: its
 * own screen, CR_K_ROUTE, and the black keys as its shortcuts, "THE ROUTING PAGE" below).
 * A PART has its sends (P_REV, P_CHOR, P_DLY) and its drive (P_DIST), no sound load touches them (but FM TONE's Drive,
 * which is the sound's: "FM TONE'S DRIVE IS ITS PRESET'S" below): the chord part's are
 * cs.fx_amt[] (cu_send / cu_send_set below: the ONE value the Sends page, the "Send" cell on KNOB 4 of an effect's
 * first page, the sound editor's FX group, KNOB 4 of the main view and CC 91 / 93 / 94 read and write, each in per
 * cent and by the same detent: cu_send_text / cu_send_step), the bass
 * part's its track's. The effects' own parameters are GLOBAL: the delay's and the reverb's are cr_fx.c's xfp[], the
 * chorus's song.g; the session keeps them (cr_settings.c CSS_FXG) and no sound load touches them */
#define CU_NFX 4u                         /* the effects (cs.fx_sel, cs.fx_amt) */
#define CU_NFXP 9u                        /* SELECT's pages */
#define CU_FXPG_SENDS 2u                  /* cs.fx_page: the Sends page (0, 1: cs.fx_sel's first and second page) */
#define CU_FXPG_FILTER 3u                 /* .. the Filter page (as Sends: cs.fx_sel stays the effect last picked) */
#define CU_FXPG_ROUTE 4u                  /* .. the Routing page (the same) */
/* a cell: an xfp id (cr_fx.c XP_*), FC_G + a song.g id, or one of these */
enum { FC_NONE = -1, FC_SEND = -2, FC_TIME = -3, FC_SPREAD = -4, FC_G = 64,     /* TIME, SPREAD: by Sync, the free or
                                                                                 * the synced parameter */
       FC_SND0 = -8 };                    /* FC_SND0 - e: the chord part's send into effect e (CU_FX), the Sends page's */
static const struct { const char *name; uint8_t send; } CU_FX[CU_NFX] = {
    {"Reverb", P_REV}, {"Chorus", P_CHOR}, {"Delay", P_DLY}, {"Drive", P_DIST}};
static const struct { uint8_t fx, page, of; int8_t c[4]; const char *label[4]; } CU_FXP[CU_NFXP] = {
    /* the Sends page (0.18, the owner 2026-10-12): the four of the sound editor's FX group, in its order, under its
     * names, on its knobs (cr_pages.c CP_PAGES[CP_FX]: Drive, Chorus, Delay, Reverb) */
    {0, CU_FXPG_SENDS, 1, {FC_SND0 - 3, FC_SND0 - 1, FC_SND0 - 2, FC_SND0 - 0}, {"Drive", "Chorus", "Delay", "Reverb"}},
    {0, 0, 2, {XP_RSIZE, XP_RDAMP, XP_RDECAY, FC_SEND}, {"Size", "Damp", "Decay", "Send"}},
    {0, 1, 2, {XP_RPRE, XP_RMOD, XP_RLOW, XP_RWIDTH}, {"Pre-delay", "Mod", "Low cut", "Width"}},
    {1, 0, 1, {FC_G + G_CRATE, FC_G + G_CDEPTH, XP_CWIDTH, FC_SEND}, {"Rate", "Depth", "Width", "Send"}},
    {2, 0, 2, {FC_TIME, FC_SPREAD, XP_DFDBK, FC_SEND}, {"Time", "Spread", "Feedback", "Send"}},
    {2, 1, 2, {XP_DSYNC, XP_DMODE, XP_DTONE, XP_DWOW}, {"Sync", "Mode", "Tone", "Wow"}},
    {3, 0, 1, {FC_NONE, FC_NONE, FC_NONE, FC_SEND}, {"", "", "", "Send"}},
    /* the master filter (0.22, GitHub issue 16; cr_fx.c "The master filter"): the last page, as it is last in the
     * chain. Filter on KNOB 4 is its On / Off; a cutoff turned switches it (and FX) on, as a send does */
    {0, CU_FXPG_FILTER, 1, {XP_FLOW, XP_FHIGH, XP_FSLOPE, XP_FON}, {"Low cut", "High cut", "Slope", "Filter"}},
    /* the routing (GitHub issue 21, second design; cr_fx.c "The routing"): no knob cells, its own screen
     * (CR_K_ROUTE): the black keys hold a block, KNOB 1 moves it, KNOB 2 the mode ("THE ROUTING PAGE" below) */
    {0, CU_FXPG_ROUTE, 1, {FC_NONE, FC_NONE, FC_NONE, FC_NONE}, {"", "", "", ""}}};
_Static_assert(XFE_CHORUS == 0 && XFE_DELAY == 1 && XFE_REVERB == 2 && XFE_FILTER == 3 && XFE_N == 4,
               "cr_screen.h rt_order: 0 Chorus, 1 Delay, 2 Reverb, 3 Filter");
static const char *const CU_XDIV[XF_NDIV] = {"1/32", "1/16T", "1/32.", "1/16", "1/8T", "1/16.", "1/8", "1/4T", "1/8.",
                                             "1/4", "1/2T", "1/4.", "1/2"};      /* cr_fx.c XF_DIV's order */
static const char *const CU_BASSMODE[4] = {"Chords Only", "Unison Bass", "Bass Single Notes", "Solo"};
static const char *const CU_VIEW[6] = {"Chord", "Keyboard", "Notes", "Geek Out", "Scope", "Geek Out 2"};
static const char *const CU_LOOPLEN[6] = {"Free", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars"};
static const char *const CU_SIG[CRL_NSIG] = {"4/4", "3/4", "6/8"};
static const char *const CU_QUANT[CRL_NQUANT] = {"none", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const char *const CU_LOOP_MODE[CRL_NMODE] = {"Overwrite", "Advance", "Overdub", "Replace", "Step"};
static const char *const CU_RATE[CRL_NRATE] = {"x1/16", "x1/12", "x1/8", "x1/6", "x1/4", "x1/2", "x3/4", "x1", "x1.5", "x2", "x4"};
enum { V_CHORD, V_KEYBOARD, V_NOTES, V_GEEK, V_SCOPE, V_GEEK2, V_N };   /* (saved: cr_settings.h CRS_VIEW; a new
                                                                         * view goes last, a record keeps its meaning) */

static struct {
    uint8_t playstyle, extadd, secret, key_on, tonic, scale, single, vel;
    int8_t transpose;
    uint8_t bass_on, bass_mode, perform_on, perf_sel, sticky;
    uint16_t bpm;
    int16_t par[CR_PM_COUNT][CR_P_COUNT];
    uint8_t fx_on, fx_sel, fx_amt[CU_NFX];
    uint8_t fx_page;                      /* the effect's page in the FX layer (0, or 1: Reverb 2, Delay 2), or
                                           * CU_FXPG_SENDS: the Sends page (cs.fx_sel stays the effect last picked:
                                           * KNOB 4 of the main view is still its send) */
    uint16_t sound, bass_sound;           /* pool positions: PRESETS (the chord part's), ALGORITHM (0 = OFF, pos + 1) */
    uint8_t pool_pos[2][NENG_SHOWN];      /* per part, per engine (ENGINE_ORDER rank): the pool position last played
                                           * (OPT + PRESETS lands there; Settings v6) */
    uint8_t view, leds;
    uint8_t view_lock;                    /* HOME held's menu, Lock: HOME tap never cycles the view (Settings v9) */
    uint8_t ch[CR_NSTREAM];               /* MIDI channel 1..16, 0 = off */
    uint8_t raw_sound;                    /* RAW also plays part 0 */
    uint8_t split;                        /* Single Notes' split point, pitch class 0..11 */
    uint8_t metro, metro_sig, metro_vol;  /* the click: on, CRL_SIG_*, level 0..100 */
    uint8_t loop_len, loop_quant, loop_count_in, loop_level;   /* SYNC (0 Free, 1..16 bars), QUANT, COUNT-IN, LEVEL */
    uint8_t loop_rate;                    /* the loop's Rate, 0..CRL_NRATE-1 (x1/16 .. x4; CRL_RATE_1; the session's) */
    uint8_t loop_slot;                    /* the slot in RAM (0..9) */
    uint8_t loop_mode, loop_keys;         /* the record mode (CRL_MODE_*); the PLAY menu's keys: 0 Play, 1 Loops (v8) */
    uint8_t key_play;                     /* the KEY layer's keys: 0 pick (a root = the tonic, MAJ / MIN = the scale),
                                           * 1 play (roots and chord keys as outside the layer); OPT tapped in the
                                           * layer (v13) */
    uint8_t key_notes;                    /* the KEY layer's page 2, Key plays: 0 Chords, 1 Notes (Key Mode blocks the
                                           * roots out of the scale and generates no chord; v14) */
    uint8_t kbd_full;                     /* .. Keyboard: 0 Chords (the chord block + 18 roots), 1 Full (27 single-note
                                           * keys, no chord block, no LOCK; v14) */
    uint8_t snap;                         /* .. Snap (KNOB 3; 0.22, cr_keysnap_t): with Key plays Notes / the Full
                                           * keyboard a root out of the scale plays the nearest note of the scale (1)
                                           * or of the chord sounding (2) instead of nothing (0, the default) */
    uint8_t numerals;                     /* .. Numerals (KNOB 4; 0.22): 1 = the Chord view shows the sounding chord's
                                           * Roman numeral above its name (cu_numeral); 2 (GitHub issue 15) the same
                                           * degree as a solfege syllable there; 0 Off, the default */
    uint8_t lock_keep, lock_push;         /* the LOCK layer (B3 held; v13): a type key keeps the latched extensions
                                           * (0: clears them, today's); the extension keys are pushed as the type
                                           * keys are (0: toggles, today's) */
    uint16_t loop_used;                   /* bit k: slot k holds a loop in flash */
    uint8_t usb_in, usb_fixed;            /* Options > USB Record (on: presented to the computer), USB Level */
    uint8_t dualcore;                     /* Options > Dual Core (v12): 1 On; read at power-on only (cr_c1_off) */
    uint8_t out_clean;                    /* Options > Output (0.19): 1 Clean, 0 Classic (fx.c fx_output_set) */
    uint8_t usb48;                        /* Options > USB Rate (0.19): 1 = ChoralRoot In offers 48 kHz too; 0, the
                                           * default: 44.1 kHz only, 0.18's device (usb.c UA_OFF_48) */
    uint8_t names_std;                    /* Options > Chord Names (0.20): 1 Standard, 0 Orchid, the default
                                           * (cr_engine.c cr_chord_names_set; docs/CHORD-NAMES.md) */
    uint8_t touch, vel_sens, at_sens;     /* Options > Key Touch (0 Fixed, 1 Dynamic, 2 Velocity + Aftertouch), Velocity
                                           * Sens and Aftertouch Sens (0 Mid, 1 Low, 2 High): docs/VELOCITY.md */
    int8_t vel_curve;                     /* Options > Velocity Curve -3 .. +3 (0: Velo's 0.7; the map's exponent x 2^(0.2 k)) */
    uint8_t sixth_up;                     /* Options > 6th with 7th (0.20): 1 Octave up, 0 In place, the default
                                           * (cr_engine.c cr_set_sixth_up; docs/CHORD-NAMES.md) */
    uint8_t layout;                       /* Options > Chord Keys (0.24, docs/CHORD-LAYOUT.md): 0 Classic, 1 FM-1 */
    uint8_t suswin;                       /* .. SUS Window, ms (30 .. 80, 5 a step; 30 the default): FM-1's type keys */
    uint8_t full_short;                   /* .. Full K Shorts: F#5 / G5 are the Full keyboard's shortcut keys */
    uint8_t midi_keys;                    /* .. MIDI Keys: the channel (1 .. 16) whose notes 53 .. 79 press the keys F3 ..
                                           * G5; 0 Off */
#if BOARD_SMK37
    uint8_t smk_pedal;                    /* Options > Pedal: the pedal jack 0 Sustain, 1 Expression (the session's:
                                           * cr_settings.h smk_pedal; cr_smk_pedal) */
#endif
} cs;

/* the layout (docs/CHORD-LAYOUT.md): Classic unless Options > Chord Keys says FM-1 (never on the SMK-37: its chord keys
 * are pads, CU_FN_KEYS 0) */
static int cu_fm1(void) { return CU_FN_KEYS && cs.layout; }
static uint32_t cu_mod_of(uint32_t k)              /* chord block key k -> cr_mod_t, 0xFF none (B3 in Classic, the
                                                    * FM-1's roots C4 C#4) */
{
    return k < 9u ? (cu_fm1() ? CU_MOD_OF_KEY_FM1[k] : CU_MOD_OF_KEY[k]) : 0xFFu;
}
static uint32_t cu_lbtn(uint32_t l)               /* layer l's button (NB: none; the LOCK layer's is LFO in FM-1) */
{
    return l == L_LOCK ? (cu_fm1() ? (uint32_t)BT_LATCH : (uint32_t)NB) : L_BTN[l];
}
static uint32_t cu_layer_of(uint32_t b)
{
    uint32_t l;
    for (l = 1; l < L_N; l++)
        if (cu_lbtn(l) == b)
            return l;
    return L_NONE;
}
#if CR_HAVE_SETTINGS
static void cr_settings_load(void);       /* cr_settings.c (included after this file): the record -> the UI */
static void cr_settings_save(void);       /* .. the automatic part now (Felucca's settings_save: the panel table too) */
static void cr_settings_reset(void);      /* .. Options > Reset Settings: the defaults, applied and saved */
static int cr_settings_commit(void);      /* .. the global save (SAVE held): the whole block and the session */
static void cr_settings_revert(void);     /* .. GLO + SAVE: what the last global save wrote, applied again */
static void cr_settings_dc_off(void);     /* .. Dual Core Off after core 1 was given up (cr_cpu1.h), saved */
static void cr_settings_dc_user(void);    /* .. the player set Dual Core: the record's from here on */
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
static void cu_post_single(void)
{
    cr_post(CRE_SINGLE, cs.single, 0, cs.split);
}
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
    cr_post(CRE_LOOP, LP_CONF, LC_RATE, cs.loop_rate);
}
static void cu_set_tempo(int32_t bpm)
{
    cs.bpm = (uint16_t)(bpm < 20 ? 20 : bpm > 300 ? 300 : bpm);
    song.g[G_BPM] = (int16_t)(cs.bpm < 40u ? 40u : cs.bpm > 240u ? 240u : cs.bpm);
    xf_bpm = cs.bpm;                              /* (the delay's sync, cr_fx.c: the whole range) */
    cr_post(CRE_TEMPO, 0, 0, cs.bpm);
}
/* THE SENDS ARE THE PARTS' (0.18, the owner 2026-10-12): each part has its own send into the reverb, the chorus and the
 * delay, and its drive, set once and kept through every sound and engine change and a power-off. The chord part's:
 * cs.fx_amt[] (the FX layer's "Send" cells, KNOB 4, CC 91 / 93 / 94; the session's fx.amt[]); the bass part's: its
 * P_DIST .. P_REV as they stand (its editor's FX group, CC 91 / 93 / 94 on the bass channel; the session's
 * CSS_FXP_BS_*). No load writes them (cr_bank.c param_kept); a preset's or a saved sound's send fields are ignored
 * on load and written as the part's on save (the record's format is as it was) */
/* FM TONE'S DRIVE IS ITS PRESET'S (the owner, 2026-10-12: "Digitone presets should save and load the drive level with
 * them and also always enable drive if this is greater than 0 on FM TONE and FX is off"). While a part's engine is FM
 * TONE its track's P_DIST is the SOUND's Drive (the AMP page's cell, 0..127): an FM TONE load writes it (cu_drive_load:
 * a factory preset's preset_t.fx[0], a user record's P_DIST field, INIT 0), a save stores it in the record's P_DIST
 * field (cr_bank.c cb_store, as it always wrote the track's), the session keeps it in the part's p[] (css_part_t),
 * turning it marks the sound edited, and FX off does not close it (cu_fx_apply leaves it). The part's own drive SEND
 * (the other engines') waits meanwhile, untouched: the chord part's in cs.fx_amt[] as ever, the bass part's in
 * cu_bdrive; it is back when the part leaves FM TONE. No stored format changed (docs/FX.md "FM TONE's Drive") */
static int16_t cu_bdrive;                         /* the bass part's drive send while its engine is FM TONE */
static int cu_quad_part(uint32_t part)
{
#if FELUCCA_QUAD
    return eng_idx(trk[part ? CR_PART_BASS : CR_PART_CHORD].eng_req) == ENGI_QUAD;
#else
    (void)part;
    return 0;
#endif
}
static int16_t *cu_bdrive_p(void)                 /* the bass part's drive send, where it is now (the session's tag) */
{
    return cu_quad_part(1) ? &cu_bdrive : &trk[CR_PART_BASS].p[P_DIST];
}
static void cu_fx_apply(void)                     /* the sends of part 0: the amounts, or 0 with FX off */
{
    uint32_t i;
    for (i = 0; i < CU_NFX; i++)
        if (CU_FX[i].send != P_DIST || !cu_quad_part(0))         /* (FM TONE's Drive: the sound's, not gated) */
            trk[CR_PART_CHORD].p[CU_FX[i].send] = (int16_t)(cs.fx_on ? cs.fx_amt[i] : 0u);
    fx_off = !cs.fx_on;                           /* (fx.c: FX off closes every part's sends: the bass part's too, which
                                                   * keeps its values) */
}
static void cu_drive_load(track_t *t, uint32_t e, int32_t d)     /* (declared at cu_load; the IRQ may be off) */
{
#if FELUCCA_QUAD
    int was = eng_idx(t->eng_req) == ENGI_QUAD, is = eng_idx(e) == ENGI_QUAD;
    if (t == &trk[CR_PART_BASS]) {
        if (is && !was)
            cu_bdrive = t->p[P_DIST];             /* the part's send waits */
        else if (was && !is)
            t->p[P_DIST] = cu_bdrive;             /* .. and is back */
    } else if (t == &trk[CR_PART_CHORD] && was && !is) {
        t->p[P_DIST] = (int16_t)(cs.fx_on ? cs.fx_amt[3] : 0u);   /* (as cu_fx_apply: CU_FX[3] is Drive) */
    }
    if (is)
        t->p[P_DIST] = (int16_t)(d < 0 ? 0 : d > 127 ? 127 : d);
#else
    (void)t; (void)e; (void)d;
#endif
}
/* a new unit's, and Options > Reset Settings': what the power-on sounds were made with (FM6 TINE EP: reverb 35,
 * chorus 45, delay 25, no drive; VA DEEP SUB: a little reverb, 10) */
static void cu_sends_default(void)
{
    static const uint8_t DEF[CU_NFX] = {35, 45, 25, 0};          /* CU_FX's order: Reverb, Chorus, Delay, Drive */
    uint32_t i;
    for (i = 0; i < CU_NFX; i++) {
        cs.fx_amt[i] = DEF[i];
        if (CU_FX[i].send == P_DIST)
            *cu_bdrive_p() = 0;                   /* (the SEND: an FM TONE bass sound keeps its Drive) */
        else
            trk[CR_PART_BASS].p[CU_FX[i].send] = (int16_t)(CU_FX[i].send == P_REV ? 10 : 0);
    }
    cu_fx_apply();
}
static void cu_sends_to_fx(void) { cu_fx_apply(); }   /* (after a load of part 0; until 0.18 its own sends became the
                                                       * FX amounts here: now the part's stay) */
/* A PART'S SEND, ONE VALUE (the owner, 2026-10-12: the editor's FX group did not show the chord part's sends as the
 * FX layer had left them "until I turn them"). The chord part's is cs.fx_amt[]; its track's P_DIST .. P_REV are only
 * what the audio reads, written by cu_fx_apply alone (the amounts, or 0 with FX off): nothing of the UI reads a send
 * from there or writes one there. The bass part's is its track's (FX off closes its sends in fx.c, the values stay).
 * id: P_DIST, P_CHOR, P_DLY or P_REV */
static int32_t cu_send_ix(uint32_t id)            /* the effect (CU_FX) a send parameter feeds; -1: not a send */
{
    uint32_t i;
    for (i = 0; i < CU_NFX; i++)
        if (CU_FX[i].send == id)
            return (int32_t)i;
    return -1;
}
static int cu_send_quad(uint32_t part, uint32_t id) { return id == P_DIST && cu_quad_part(part); }   /* FM TONE's
                                                   * Drive: the sound's, in the part's track (above, at cu_fx_apply) */
static int32_t cu_send(uint32_t part, uint32_t id)
{
    int32_t i = cu_send_ix(id);
    return !part && i >= 0 && !cu_send_quad(0, id) ? (int32_t)cs.fx_amt[i]
                                                   : trk[part ? CR_PART_BASS : CR_PART_CHORD].p[id];
}
static void cu_send_set(uint32_t part, uint32_t id, int32_t v)   /* (the chord part's: FX on, as every send turned) */
{
    int32_t i = cu_send_ix(id);
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    if (!part && i >= 0 && !cu_send_quad(0, id)) {
        cs.fx_amt[i] = (uint8_t)v;
        cs.fx_on = 1;
        cu_fx_apply();
    } else {
        track_t *t = &trk[part ? CR_PART_BASS : CR_PART_CHORD];
        int32_t was = t->p[id];
        fm1_irq_off();
        t->p[id] = (int16_t)v;
        fm1_irq_on();
        if (cu_send_quad(part, id) && v != was) { /* FM TONE's Drive is the sound's: an edit of it (cu_edited); FX
                                                   * stays as it is: this drive sounds with FX off too */
            psnd[part & 1u].edited = 1;
            t->user = 0;
        }
    }
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
/* the page of the FX layer: cs.fx_sel's, cs.fx_page */
static uint32_t cu_fx_pg(void)
{
    uint32_t i, first = 1;
    if (cs.fx_page == CU_FXPG_SENDS)
        return 0;
    if (cs.fx_page == CU_FXPG_ROUTE)
        return CU_NFXP - 1u;
    if (cs.fx_page == CU_FXPG_FILTER)
        return CU_NFXP - 2u;
    for (i = 1; i + 2u < CU_NFXP; i++)
        if (CU_FXP[i].fx == cs.fx_sel % CU_NFX) {
            if (CU_FXP[i].page == cs.fx_page)
                return i;
            if (!CU_FXP[i].page)
                first = i;
        }
    return first;
}
static const char *cu_fx_pgname(void)             /* the page's name: the effect's, Sends, Filter, Routing (the traces,
                                                   * the dump) */
{
    return cs.fx_page == CU_FXPG_SENDS ? "Sends" : cs.fx_page == CU_FXPG_FILTER ? "Filter"
         : cs.fx_page == CU_FXPG_ROUTE ? "Routing" : CU_FX[cs.fx_sel % CU_NFX].name;
}
/* THE ROUTING PAGE (GitHub issue 21, second design; the owner: "the routing page should show a like route signal
 * flow as boxes ... use the black keys for shortcuts ... while holding it down, it will raise up and you can use Knob
 * 1 to move it around"; cr_fx.c "The routing"). The FX layer's last page draws CR_K_ROUTE (cr_draw.c cr_p_route) and
 * owns the root keys while it shows (cu_layer_keys: nothing plays; the chord block keeps its job): C#4 Chorus, D#4
 * Delay, F#4 Reverb, G#4 Filter held = that block lifted (cu_rt.held), KNOB 1 moves it a place a detent (insert: the
 * others shift; clamped), its release lands it; A#4 tapped and KNOB 2 (one a detent: clockwise Chain) the mode. In
 * Parallel the effects' order is not heard: a held effect lifts, KNOB 1 leaves it; the Filter held, KNOB 1 moves it
 * between its two places, before the buses (left) and after them (right; cu_fx_move). The
 * keys are FM-1's layout's (C4 = key 7); Classic's C#4 is a chord key there, so Chorus has no shortcut in Classic.
 * The rest of the keys do nothing here. Both values are xfp[] (the session's tags 35 and 36) */
static const char *const CU_RT_NAME[XFE_N] = {"Chorus", "Delay", "Reverb", "Filter"};
static struct { uint8_t held, key; } cu_rt;       /* the block held + 1 (0: none), its key + 1 */
static uint32_t cu_rt_ord(void) { return (uint32_t)xfp[XP_ORDER] < 24u ? (uint32_t)xfp[XP_ORDER] : 0u; }
static const char *cu_fx_posname(uint32_t k)      /* the block at the chain's position k: "Chorus" .. "Filter" */
{
    return CU_RT_NAME[XF_ORD[cu_rt_ord()][k % XFE_N]];
}
static int32_t cu_rt_block_of(uint32_t k)         /* a root key's block on the Routing page (-1: none; XFE_N: A#4) */
{
    return k == 8u ? XFE_CHORUS : k == 10u ? XFE_DELAY : k == 13u ? XFE_REVERB : k == 15u ? XFE_FILTER
         : k == 17u ? (int32_t)XFE_N : -1;
}
static int cu_rt_key(uint32_t l, uint32_t k)      /* key k is one of the Routing page's five shortcuts, the page showing */
{
    return l == L_FX && cs.fx_page == CU_FXPG_ROUTE && cu_rt_block_of(k) >= 0;
}
/* block b moves s places along the chain (insert: the ones it passes shift back; one place a detent, clamped at the
 * ends); XP_ORDER becomes the permutation it makes (XF_ORD's index). 1: it moved. Parallel (the third pass, the
 * owner: "move the filter to put it before the FX"): only the Filter moves, between its two places, before the buses
 * (position 0: left) and after them (position 3: right); the effects keep their order (Chain's, not heard here) */
static int cu_fx_move(uint32_t b, int32_t s)
{
    uint8_t o[XFE_N];
    uint32_t i, j;
    int32_t p = 0, q;
    for (i = 0; i < XFE_N; i++) {
        o[i] = XF_ORD[cu_rt_ord()][i];
        if (o[i] == b)
            p = (int32_t)i;
    }
    if (xfp[XP_RMODE] != 1 && b != XFE_FILTER)
        return 0;
    q = xfp[XP_RMODE] != 1 ? (s < 0 ? 0 : p ? p : (int32_t)XFE_N - 1) : p + s;   /* (Parallel: already after stays) */
    q = q < 0 ? 0 : q > (int32_t)XFE_N - 1 ? (int32_t)XFE_N - 1 : q;
    if (q == p)
        return 0;
    for (; p < q; p++)
        o[p] = o[p + 1];
    for (; p > q; p--)
        o[p] = o[p - 1];
    o[q] = (uint8_t)b;
    for (i = 0; i < 24u; i++) {
        for (j = 0; j < XFE_N && XF_ORD[i][j] == o[j]; j++)
            ;
        if (j == XFE_N)
            xfp[XP_ORDER] = (int16_t)i;
    }
    return 1;
}
/* a cell's parameter: the delay's Time and Spread are the synced or the free one (both kept: Sync changes which) */
static int32_t cu_fx_id(int32_t c)
{
    return c == FC_TIME ? (xfp[XP_DSYNC] ? XP_DDIV : XP_DTIME) : c == FC_SPREAD ? (xfp[XP_DSYNC] ? XP_DRATIO : XP_DSPREAD) : c;
}
static void cu_fx_unit(char *out, uint32_t v, const char *unit, uint32_t n)   /* "250 ms" */
{
    cu_int(out, (int32_t)v, 0, n);
    cu_cat(out, unit, n);
}
static void cu_fx_hz(char *out, uint32_t hz, uint32_t n)   /* "130 Hz", "2.1 kHz", "16.6 kHz" */
{
    if (hz < 1000u) {
        cu_fx_unit(out, hz, " Hz", n);
        return;
    }
    cu_int(out, (int32_t)(hz / 1000u), 0, n);
    cu_cat(out, ".", n);
    cu_int(out + (hz >= 10000u ? 3u : 2u), (int32_t)(hz % 1000u / 100u), 0, n - 3u);
    cu_cat(out, " kHz", n);
}
static void cu_send_text(int32_t v, char *out, uint32_t n);
/* a parameter's value as the knob row shows it: its unit where it has one ("250 ms", "1/8.", "2.4 s", "130 Hz",
 * "0.80 Hz"), a name ("Stereo"), else the number 0..127 */
static void cu_fx_val(int32_t id, char *out, uint32_t n)
{
    static const char *const RATIO[4] = {"1:1", "3:4", "2:3", "1:2"};
    int32_t v;
    if (id >= FC_G) {                             /* the chorus's (song.g): rate in Hz, depth a number */
        const param_desc_t *d = &GP[id - FC_G];
        char val[12];
        const char *unit;
        v = song.g[id - FC_G];
        if (d->fmt != F_LFOHZ) {
            cu_int(out, v, 0, n);
            return;
        }
        param_format(d, v, val, &unit);
        out[0] = 0;
        if (val[0] == '.')                        /* ".80" -> "0.80" */
            cu_cpy(out, "0", n);
        cu_cat(out, val, n);
        cu_cat(out, " ", n);
        cu_cat(out, unit, n);
        return;
    }
    v = xfp[id];
    switch (id) {
    case XP_DTIME: cu_fx_unit(out, xf_ms((uint32_t)v), " ms", n); break;
    case XP_DDIV: cu_cpy(out, CU_XDIV[(uint32_t)v % XF_NDIV], n); break;
    case XP_DSPREAD:                              /* R's time as a part of L's: "x1.00" .. "x0.50" */
        v = ((254 - v) * 100 + 127) / 254;
        cu_cpy(out, v >= 100 ? "x1." : "x0.", n);
        cu_2d(out + 3, (uint32_t)(v % 100), n - 3u);
        break;
    case XP_DRATIO: cu_cpy(out, RATIO[v & 3], n); break;
    case XP_DSYNC: cu_cpy(out, v ? "Tempo" : "Free", n); break;
    case XP_DMODE: cu_cpy(out, v ? "Ping-pong" : "Stereo", n); break;
    case XP_RDECAY:
        if (v >= 127) {
            cu_cpy(out, "Inf", n);
        } else {
            uint32_t ms = xf_rt_ms((uint32_t)v);
            if (ms < 10000u) {                    /* "2.4 s" */
                cu_int(out, (int32_t)(ms / 1000u), 0, n);
                cu_cat(out, ".", n);
                cu_fx_unit(out + 2, ms % 1000u / 100u, " s", n - 2u);
            } else {
                cu_fx_unit(out, ms / 1000u, " s", n);
            }
        }
        break;
    case XP_RPRE: cu_fx_unit(out, xf_pre_ms((uint32_t)v), " ms", n); break;
    case XP_RLOW: cu_fx_unit(out, xf_lowcut_hz((uint32_t)v), " Hz", n); break;
    case XP_FLOW:                                 /* the master filter's: Off at the bottom */
        if (v <= 0)
            cu_cpy(out, "Off", n);
        else
            cu_fx_hz(out, xq_low_hz((uint32_t)v), n);
        break;
    case XP_FHIGH:                                /* .. Off at the top */
        if (v >= 127)
            cu_cpy(out, "Off", n);
        else
            cu_fx_hz(out, xq_high_hz((uint32_t)v), n);
        break;
    case XP_FSLOPE: cu_cpy(out, v ? "12 dB" : "6 dB", n); break;
    case XP_FON: cu_cpy(out, v && cs.fx_on ? "On" : "Off", n); break;   /* (FX off: the filter is out, as a send) */
    default: cu_int(out, v, 0, n); break;
    }
}
/* the glyph and its values (Q8 of 255) for cell code c (FORMAT.md "cell glyphs"; docs/INTEGRATION.md 3) */
static void cu_fx_glyph(int32_t c0, cr_cell_t *c)
{
    static const uint8_t SPR[4] = {0, 128, 170, 255};   /* the synced spreads as the free one's travel: 1, 3/4, 2/3, 1/2 */
    int32_t id = cu_fx_id(c0);
    uint32_t q = id >= FC_G ? (uint32_t)song.g[id - FC_G] * 255u / 127u
               : (uint32_t)(xfp[id] - XFP[id].min) * 255u / (uint32_t)(XFP[id].max - XFP[id].min);
    uint32_t tq = xfp[XP_DSYNC] ? (uint32_t)xfp[XP_DDIV] * 255u / (XF_NDIV - 1u) : (uint32_t)xfp[XP_DTIME] * 255u / 127u;
    c->pct = (uint8_t)q;
    switch (c0) {
    case XP_RSIZE: c->glyph = CR_G_ROOM; break;
    case XP_RDAMP: c->glyph = CR_G_MOON; c->pct = (uint8_t)(255u - q); break;    /* more damping, darker */
    case XP_RDECAY: c->glyph = CR_G_TAIL; break;
    case XP_RPRE: c->glyph = CR_G_PRE; break;
    case XP_RMOD: c->glyph = CR_G_LFO; c->pct = 64; c->pct2 = (uint8_t)q; break;   /* two cycles, the depth */
    case XP_RLOW: c->glyph = CR_G_CUT; break;
    case XP_RWIDTH: case XP_CWIDTH: c->glyph = CR_G_WIDTH; break;
    case FC_TIME: case XP_DFDBK:                  /* the time (spacing), the feedback (decay) */
        c->glyph = CR_G_ECHOES;
        c->pct = (uint8_t)tq;
        c->pct2 = (uint8_t)((uint32_t)xfp[XP_DFDBK] * 255u / 127u);
        break;
    case FC_SPREAD:                               /* two rows of echoes: L's spacing, R's a part of it */
        c->glyph = CR_G_SPREAD;
        c->pct = (uint8_t)tq;
        c->pct2 = xfp[XP_DSYNC] ? SPR[xfp[XP_DRATIO] & 3] : (uint8_t)((uint32_t)xfp[XP_DSPREAD] * 255u / 127u);
        break;
    case XP_DSYNC: c->glyph = CR_G_SYNC; break;   /* notes on a beat, or a clock face */
    case XP_DMODE: c->glyph = CR_G_PING; break;   /* two straight paths, or one bouncing between the walls */
    case XP_DTONE: c->glyph = CR_G_MOON; break;
    case XP_DWOW: c->glyph = CR_G_LFO; c->pct = 20; c->pct2 = (uint8_t)q; break;
    case XP_FLOW: c->glyph = CR_G_CUT; break;     /* the master filter: the low cut's corner, the high cut's moon
                                                   * (full: open); Slope and Filter are text */
    case XP_FHIGH: c->glyph = CR_G_MOON; break;
    case FC_G + G_CRATE: case FC_G + G_CDEPTH:    /* rate, depth (0..127) */
        c->glyph = CR_G_LFO;
        c->pct = (uint8_t)((uint32_t)song.g[G_CRATE] * 255u / 127u);
        c->pct2 = (uint8_t)((uint32_t)song.g[G_CDEPTH] * 255u / 127u);
        break;
    default: break;
    }
}
/* A SEND'S READ-OUT AND ITS STEP, ONE SCALE (the owner, 2026-10-13: the same send read "74" in Reverb 1's Send cell
 * and "75%" on the Sends page; "make the old per effect send parameters also show as 0-100% to match the first send
 * page"). Every screen that shows a part's send shows cu_send_text, every knob that turns one steps cu_send_step: the
 * Sends page, the Send cell of an effect's page, KNOB 4 of the main view and its popup, the CC 91 / 93 / 94 popups,
 * the sound editor's FX group (cr_edit.c ce_text, ce_knob), either part's.
 * The scale: per cent of the stored 0..127, rounded to the nearest (params.c param_format's F_PCT, as the editor
 * always read it): 0 is 0 %, 127 is 100 %, and only they are.
 * The step: the knob's speed (cr_pages.c kn_lin: 1 of 127 a slow detent, up to 5 a quick one), clamped, so both
 * ends are reached exactly; snap (OPT held, the editor's SHIFT): 0 16 32 .. 112 127 (cu_kval) */
static uint32_t cu_send_pct(int32_t v)
{
    v = v < 0 ? 0 : v > 127 ? 127 : v;
    return ((uint32_t)v * 100u + 63u) / 127u;
}
static void cu_send_text(int32_t v, char *out, uint32_t n)       /* "46%" */
{
    cu_int(out, (int32_t)cu_send_pct(v), 0, n);
    cu_cat(out, "%", n);
}
/* a knob's value lo .. hi stepped s detents: at the knob's speed, or snapped (OPT held: cr_pages.c kn_snap_rng on the
 * range's grid, kn_grid_rng). The values of the layers and the main view that have no descriptor */
static int32_t cu_kval(int32_t v, int32_t s, int32_t lo, int32_t hi, uint32_t snap)
{
    return snap ? kn_snap_rng(v, s, lo, hi, kn_grid_rng(lo, hi, lo < 0), 0) : kn_lin(v, s, lo, hi);
}
static int32_t cu_send_step(int32_t v, int32_t s, uint32_t snap)
{
    return cu_kval(v, s, 0, 127, snap);
}
/* the effect a cell of page pg sends into: its "Send" cell's, a cell of the Sends page's; -1: another cell */
static int32_t cu_fx_send_of(uint32_t pg, int32_t c0)
{
    return c0 == FC_SEND ? (int32_t)CU_FXP[pg].fx : c0 <= FC_SND0 && c0 > FC_SND0 - (int32_t)CU_NFX ? FC_SND0 - c0 : -1;
}
/* the fx layer's cells: the page's four (a missing one: no cell, a dim dash); "Send" is the chord part's send into
 * the effect (Drive: its amount, the clip glyph; the others the dry / wet squares; "off" while FX is off). The Sends
 * page: the four sends. Every send cell reads as the sound editor's FX group reads it (cu_send_text: "46%") */
static void cu_fx_cells(cr_screen_t *s)
{
    uint32_t pg = cu_fx_pg(), k;
    for (k = 0; k < 4u; k++) {
        cr_cell_t *c = &s->cell[0][k];
        int32_t c0 = CU_FXP[pg].c[k], e = cu_fx_send_of(pg, c0);
        if (c0 == FC_NONE)
            continue;
        c->flags = CR_CF_ON;
        cu_cpy(c->label, CU_FXP[pg].label[k], sizeof c->label);
        if (e >= 0) {
            uint32_t id = CU_FX[e].send;
            int32_t v = cu_send(0, id);
            c->glyph = id == P_DIST ? CR_G_CLIP : CR_G_MIX;
            if (!cs.fx_on && !cu_send_quad(0, id)) {  /* (FM TONE's Drive sounds with FX off: its cell stays live) */
                cu_cpy(c->value, "off", sizeof c->value);
            } else {
                cu_send_text(v, c->value, sizeof c->value);
                c->pct = (uint8_t)((uint32_t)v * 255u / 127u);
            }
            continue;
        }
        cu_fx_val(cu_fx_id(c0), c->value, sizeof c->value);
        cu_fx_glyph(c0, c);
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
    cr_mkeys_ch = cs.midi_keys;                   /* MIDI Keys (0 Off; never a part's channel: opt_set) */
    cr_post(CRE_STREAM, CR_STREAM_RAW, 0, cs.ch[CR_STREAM_RAW] || cs.raw_sound);
}

/* ------------------------------------------------------------- Options --- */
enum { O_STYLE, O_EXTADD, O_SECRET,
       O_LAYOUT, O_SUSWIN, O_FULLSHORT, O_MIDI_KEYS,   /* the chord layouts (0.24, docs/CHORD-LAYOUT.md): together, after
                                          * Secret Chords (the owner, 2026-10-16) */
       O_SIXTH, O_VEL, O_TOUCH, O_VSENS, O_VCURVE, O_ATSENS,
#if BOARD_SMK37
       O_PEDAL,                           /* the SMK-37's pedal jack (cr_smk_pedal): not in the FM-1's list */
#endif
       O_BASSMODE, O_SINGLE, O_SPLIT, O_CH_MAIN, O_CH_BASS, O_CH_RAW, O_RAW_SOUND,
       O_CLOCK, O_VIEW, O_MOTION, O_NAMES, O_LEDS, O_HOLD, O_USB_IN, O_USB_LEVEL, O_USB_RATE, O_DUALCORE, O_OUTPUT,
       O_VERSION,
       O_CALIB,
       O_RESET,                           /* Reset Settings: the last entry of a normal session */
       O_SAFE, O_ERASE,
       O_N };
#define O_N_NORMAL O_SAFE                 /* Safe Mode and Flash Data: listed in SAFE MODE only (core.h cr_safe) */
static const char *const O_NAME[O_N] = {"Play Style", "Extension Addition", "Secret Chords",
    "Chord Keys", "SUS Window", "Full K Shorts", "MIDI Keys", "6th with 7th",
    "Velocity", "Key Touch", "Velocity Sens", "Velocity Curve", "Aftertouch Sens",
#if BOARD_SMK37
    "Pedal",
#endif
    "Bass Behaviour", "Single Notes", "Split Point", "MIDI Perform", "MIDI Bass", "MIDI Raw Chord", "Raw Chord Sound",
    "MIDI Clock", "View", "Motion", "Chord Names", "LEDs", "Hold Time", "USB Record", "USB Level", "USB Rate", "Dual Core", "Output",
    "Version",
    "Calibrate", "Reset Settings", "Safe Mode", "Flash Data"};
static const int16_t O_MAX[O_N] = {2, 1, 2, 1, 10, 1, 16, 1, 127, CU_TOUCH_MAX, 2, CU_VCURVE_MAX, CU_ATSENS_MAX,
#if BOARD_SMK37
                                   1,
#endif
                                   3, 2, 11, 16, 16,
                                   16, 1, 2, V_N - 1, CR_MOTION_N - 1, 1, 1, 3, 1, 1, 1, 1, 1, 0, 0, 0, 0, 0};
static uint32_t cu_opt_n(void) { return cr_safe ? O_N : O_N_NORMAL; }
/* an entry listed (docs/CHORD-LAYOUT.md): the chord layouts' four not on the SMK-37 (its chord keys are pads), the
 * SUS Window only while Chord Keys is FM-1. SELECT skips the others (cu_opt_next); their values are kept */
static int cu_opt_vis(uint32_t o)
{
    if (o >= cu_opt_n())
        return 0;
    if (o == O_LAYOUT || o == O_SUSWIN || o == O_FULLSHORT || o == O_MIDI_KEYS)
        return CU_FN_KEYS && (o != O_SUSWIN || cs.layout);
    return 1;
}
static uint32_t cu_opt_next(uint32_t from, int32_t s)   /* SELECT in the Options: s listed entries on from entry
                                                    * from, stopping at the ends */
{
    int32_t o = (int32_t)from, d = s < 0 ? -1 : 1, n = s < 0 ? -s : s;
    while (n-- > 0) {
        int32_t t = o + d;
        while (t >= 0 && t < (int32_t)cu_opt_n() && !cu_opt_vis((uint32_t)t))
            t += d;
        if (t < 0 || t >= (int32_t)cu_opt_n())
            break;
        o = t;
    }
    return (uint32_t)o;
}
static uint8_t cu_erase_ask;              /* Options > Flash Data: OCT+ pressed once (the second erases) */
static uint8_t cu_reset_ask;              /* Options > Reset Settings: OCT+ pressed once (the second resets) */

/* Options > USB Record / USB Level / USB Rate (docs/USB-AUDIO.md; named from the FM-1's side: Record = the computer
 * records the FM-1, the device "ChoralRoot In"): USB Level at once (fx.c), USB Record and USB Rate once the setting
 * has rested 0.6 s (usb.c ua_off_set / ua_off_apply: the FM-1 leaves the bus for a second and the computer reads
 * the new configuration; never under a SysEx transfer). USB Record Off: the serial console instead (usb.c
 * usb_cdc_on). USB Rate (0.19, the owner 2026-10-12): 44.1 kHz, the default, is 0.18's device; 44.1 + 48 kHz adds
 * the second rate for the computer to choose. The emulator has no USB: the settings are kept and saved only */
/* Options > Key Touch and the sensitivities to the engine (mod.c cr_touch_*) and the scan (the driven samples off
 * with it Fixed: hal/fm1_input.h fm1_an.drive); docs/VELOCITY.md */
static void cu_touch_apply(void)
{
    cr_touch_mode = cs.touch;
    cr_touch_vsens = cs.vel_sens;
    cr_touch_atsens = cs.at_sens;
#ifdef CR_SENSE_KEYS
    cr_touch_curve = (uint8_t)(cs.vel_curve + 3);
    fm1_an.drive = cs.touch ? SN_DRIVE : 0u;
    fm1_an.probe = cs.touch ? 1u : 0u;             /* the top row's probe: a third frame phase while the keys sense */
#endif
}
static void cu_usb_apply(void)
{
    fx_usb_fixed = cs.usb_fixed;
#if FELUCCA_UAC
    ua_off_set(UA_OFF_IN, cs.usb_in, fm1_ms);
    ua_off_set(UA_OFF_48, cs.usb48, fm1_ms);
#endif
}

static void cu_layout_set(uint32_t fm1);           /* Options > Chord Keys (below, with the keys) */
static void cu_fsc_set(uint32_t on);              /* Options > Full K Shorts */
static int32_t opt_get(uint32_t o)
{
    switch (o) {
    case O_STYLE: return cs.playstyle;
    case O_EXTADD: return cs.extadd;
    case O_SECRET: return cs.secret;
    case O_LAYOUT: return cs.layout;
    case O_SUSWIN: return (cs.suswin - 30) / 5;
    case O_FULLSHORT: return cs.full_short;
    case O_MIDI_KEYS: return cs.midi_keys;
    case O_SIXTH: return cs.sixth_up;
    case O_VEL: return cs.vel;
    case O_TOUCH: return cs.touch;
    case O_VSENS: return cs.vel_sens;
    case O_VCURVE: return cs.vel_curve + 3;
    case O_ATSENS: return cs.at_sens;
#if BOARD_SMK37
    case O_PEDAL: return cs.smk_pedal;
#endif
    case O_BASSMODE: return cs.bass_mode;
    case O_SINGLE: return cs.single;
    case O_SPLIT: return cs.split;
    case O_CH_MAIN: case O_CH_BASS: case O_CH_RAW: return cs.ch[o - O_CH_MAIN];
    case O_RAW_SOUND: return cs.raw_sound;
    case O_CLOCK: return cr_route.clock_in ? 2 : cr_route.clock_out;
    case O_VIEW: return cs.view;
    case O_MOTION: return cr_motion;
    case O_NAMES: return cs.names_std;
    case O_LEDS: return cs.leds;
    case O_HOLD: return settings_hold % 4u;
    case O_USB_IN: return cs.usb_in;
    case O_USB_LEVEL: return cs.usb_fixed;
    case O_USB_RATE: return cs.usb48;
    case O_DUALCORE: return cs.dualcore;
    case O_OUTPUT: return cs.out_clean;
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
    case O_LAYOUT: cu_layout_set((uint32_t)v); break;
    case O_SUSWIN: cs.suswin = (uint8_t)(30 + 5 * v); break;
    case O_FULLSHORT: cu_fsc_set((uint32_t)v); break;
    case O_MIDI_KEYS: {                               /* (a channel of its own: the parts' are skipped) */
        int32_t d = v > cs.midi_keys ? 1 : -1;
        while (v && (v == cs.ch[CR_STREAM_MAIN] || v == cs.ch[CR_STREAM_BASS]))
            v += d;
        if (v < 0 || v > 16)
            break;
        cs.midi_keys = (uint8_t)v;
        cu_route();
        break;
    }
    case O_SIXTH: cs.sixth_up = (uint8_t)v; cr_post(CRE_SIXTH, 0, 0, v); break;   /* (the chords built from now on) */
    case O_VEL: cs.vel = (uint8_t)v; break;
    case O_TOUCH: cs.touch = (uint8_t)v; cu_touch_apply(); break;
    case O_VSENS: cs.vel_sens = (uint8_t)v; cu_touch_apply(); break;
    case O_VCURVE: cs.vel_curve = (int8_t)(v - 3); cu_touch_apply(); break;
    case O_ATSENS: cs.at_sens = (uint8_t)v; cu_touch_apply(); break;
#if BOARD_SMK37
    case O_PEDAL: cs.smk_pedal = (uint8_t)v; break;   /* (cr_smk_pedal takes the change: a held sustain let go, the
                                                       * expression back to full, the rest learnt again) */
#endif
    case O_BASSMODE: cs.bass_mode = (uint8_t)v; cr_post(CRE_BASS_MODE, 0, 0, v); break;
    case O_SINGLE: cs.single = (uint8_t)v; cu_post_single(); break;
    case O_SPLIT: cs.split = (uint8_t)v; cu_post_single(); break;
    case O_CH_MAIN: case O_CH_BASS: case O_CH_RAW:
        cs.ch[o - O_CH_MAIN] = (uint8_t)v;
        if (o != O_CH_RAW && v && v == cs.midi_keys) {   /* a part on MIDI Keys' channel: MIDI Keys off */
            cs.midi_keys = 0;
            cu_message("MIDI Keys off", CR_COL_WHITE);
        }
        cu_route();
        break;
    case O_RAW_SOUND: cs.raw_sound = (uint8_t)v; cu_route(); break;
    case O_CLOCK: cr_route.clock_in = (uint8_t)(v == 2); cr_route.clock_out = (uint8_t)(v == 1); break;
    case O_VIEW: cs.view = (uint8_t)v; break;
    case O_MOTION: cr_motion = (uint8_t)v; break;
    case O_NAMES: cs.names_std = (uint8_t)v; cr_chord_names_set(v); break;   /* (the names only: docs/CHORD-NAMES.md) */
    case O_LEDS: cs.leds = (uint8_t)v; break;
    case O_HOLD: settings_hold = (uint8_t)v; break;
    case O_USB_IN: cs.usb_in = (uint8_t)v; cu_usb_apply(); break;
    case O_USB_LEVEL: cs.usb_fixed = (uint8_t)v; cu_usb_apply(); break;
    case O_USB_RATE: cs.usb48 = (uint8_t)v; cu_usb_apply(); break;
    case O_DUALCORE:                                  /* saved; core 1 starts (or not) at the next power-on. The
                                                       * single-core package: not a setting (the record keeps what
                                                       * it holds, for the standard package) */
        if (CR_SINGLE_CORE)
            break;
        cs.dualcore = (uint8_t)v;
        cr_c1_barred = 0;                             /* (the player decided: no word of the bar any more) */
#if CR_HAVE_SETTINGS
        cr_settings_dc_user();
#endif
        break;
    case O_OUTPUT: cs.out_clean = (uint8_t)v; fx_output_set((uint32_t)v, 0); break;   /* (fx.c: faded over) */
    default: break;
    }
}
/* The unit's ID (HISTORY 3y66, docs/INSTALL-COMPAT.md "Several FM-1s"): 8 hex digits, the CRC-32 of the flash's
 * 128-bit unique ID, which the device reads once at power-on (cr_shim.c persist_boot); "" when there is none (the
 * emulator, SAFE MODE, a flash that answers no ID). Read only: INFO's tag 49 08 (cr_backup.c) and the line under
 * Options > Version's value say it; tools/fm1_install.py --unit and the web pages pick a unit by it */
static char cr_unit_id[9];
#if FELUCCA_FLASH
/* the flash's 16 bytes (fm1_flash.h fl_uid_ram) -> cr_unit_id: their CRC-32 (storage.c's, zlib's) as 8 hex digits.
 * Sixteen equal bytes (00 or FF: a part that answers no ID) are no ID */
static void cr_unit_id_set(const uint8_t *u)
{
    static const char HEX[] = "0123456789ABCDEF";
    uint32_t i, c;
    cr_unit_id[0] = 0;
    for (i = 1; i < 16u && u[i] == u[0]; i++)
        ;
    if (i == 16u)
        return;
    c = st_crc32(u, 16u);
    for (i = 0; i < 8u; i++)
        cr_unit_id[i] = HEX[(c >> (28u - 4u * i)) & 15u];
    cr_unit_id[8] = 0;
}
#endif

/* Options > Version's value (the owner, 2026-10-14; HISTORY 3y64): v whole when it fits n (the value line is
 * cr_screen.h's value[24]: 23 characters), else without its leading "ChoralRoot ": the single-core beta's
 * ChoralRoot 0.22-beta1 1-core (28) reads 0.22-beta1 1-core. Still too long: cut, as cu_cpy cuts. The display
 * alone: the INFO reply over SysEx, the backups, the splash keep CR_VERSION whole */
static void cu_version_text(char *d, const char *v, uint32_t n)
{
    static const char PFX[] = "ChoralRoot ";
    uint32_t len = 0;
    while (v[len])
        len++;
    if (len >= n && len > sizeof PFX - 1u && !memcmp(v, PFX, sizeof PFX - 1u))
        v += sizeof PFX - 1u;
    cu_cpy(d, v, n);
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
    case O_LAYOUT: cu_cpy(d, v ? "FM-1" : "Classic", n); break;
    case O_SUSWIN: cu_int(d, 30 + 5 * v, 0, n); cu_cat(d, " ms", n); break;
    case O_FULLSHORT: cu_cpy(d, ONOFF[v & 1], n); break;
    case O_MIDI_KEYS:
        if (!v) cu_cpy(d, "Off", n);
        else { cu_cpy(d, "ch ", n); cu_int(d + 3, v, 0, n > 3u ? n - 3u : 0u); }
        break;
    case O_SIXTH: cu_cpy(d, v ? "Octave up" : "In place", n); break;
    case O_VEL: cu_int(d, v, 0, n); break;
    case O_TOUCH: cu_cpy(d, v == 3 ? "Vel + Poly AT" : v == 2 ? "Vel + Aftertouch" : v ? "Dynamic" : "Fixed", n); break;
#if BOARD_SMK37
    case O_VCURVE: case O_ATSENS: cu_cpy(d, "FM-1 only", n); break;   /* (CU_VCURVE_MAX, CU_ATSENS_MAX) */
    case O_VSENS: cu_cpy(d, v == 2 ? "High" : v ? "Low" : "Mid", n); break;
    case O_PEDAL: cu_cpy(d, v ? "Expression" : "Sustain", n); break;
#else
    case O_VSENS: case O_ATSENS: cu_cpy(d, v == 2 ? "High" : v ? "Low" : "Mid", n); break;
    case O_VCURVE: cu_int(d, v - 3, 1, n); break;
#endif
    case O_BASSMODE: cu_cpy(d, CU_BASSMODE[v & 3], n); break;
    case O_SINGLE: cu_cpy(d, v == 2 ? "Split Bass" : v ? "Split Oct" : "Full Octave", n); break;
    case O_SPLIT: cu_cpy(d, CU_NOTE[v % 12], n); cu_cat(d, cs.single ? "" : " (Split off)", n); break;
    case O_RAW_SOUND: cu_cpy(d, ONOFF[v & 1], n); break;
    case O_CH_MAIN: case O_CH_BASS: case O_CH_RAW:
        if (!v) cu_cpy(d, "Off", n);
        else { cu_cpy(d, "ch ", n); cu_int(d + 3, v, 0, n > 3u ? n - 3u : 0u); }
        break;
    case O_CLOCK: cu_cpy(d, CLOCK[v % 3], n); break;
    case O_VIEW: cu_cpy(d, CU_VIEW[v % V_N], n); break;
    case O_MOTION: cu_cpy(d, MOTION[v % 3], n); break;
    case O_NAMES: cu_cpy(d, v ? "Standard" : "Orchid", n); break;
    case O_LEDS: cu_cpy(d, LEDS[v & 1], n); break;
    case O_HOLD: cu_int(d, HOLD_MS[v & 3], 0, n); cu_cat(d, " ms", n); break;
    case O_USB_IN: cu_cpy(d, ONOFF[v & 1], n); break;
    case O_USB_LEVEL: cu_cpy(d, v ? "Fixed" : "Master", n); break;
    case O_USB_RATE: cu_cpy(d, v ? "44.1 + 48 kHz" : "44.1 kHz", n); break;
    case O_DUALCORE:
        if (CR_SINGLE_CORE) { cu_cpy(d, "1 core build", n); break; }   /* (the single-core package: unavailable) */
        cu_cpy(d, ONOFF[v & 1], n); cu_cat(d, (v & 1) == !cr_c1_off || cr_c1_barred ? "" : " \267 restart", n);
        break;
    case O_OUTPUT: cu_cpy(d, v ? "Clean" : "Classic", n); break;
    case O_VERSION: cu_version_text(d, CR_VERSION, n); break;
    case O_CALIB: cu_cpy(d, "OCT+ starts", n); break;
    case O_RESET: cu_cpy(d, cu_reset_ask ? "OCT+ again: reset" : "all options to default", n); break;
    case O_SAFE: cu_cpy(d, "flash data skipped", n); break;
    case O_ERASE: cu_cpy(d, cu_erase_ask ? "OCT+ again: erase" : "erase and reboot", n); break;
    default: break;
    }
}

/* ------------------------------------------------------------ UI state --- */
enum { PG_NONE, PG_EDIT, PG_SAVE };
enum { PU_METER, PU_VOICING, PU_BASS_VOICING };
/* the screen when idle (no key, button, knob or MIDI channel message in; a loop playing, the arp, the metronome are
 * not input): the logo (the stripes) after CR_IDLE_MS, then the screen off after CR_SCREEN_OFF_MS: one black frame
 * (cr_draw.c cr_draw_black), then no LCD writes while off; the UI keeps ticking: LEDs, looper, MIDI. The panel is a
 * TFT: a black frame ends the image retention ("burn"). The backlight stays on, and the panel is not sent DISPOFF:
 * the backlight's pin, PA2, also keeps the key matrix / 595 chain alive (hal/fm1_lcd_hw.h: the screen off of
 * 0.14 / 0.15 raised it, and no key, button or knob sounded or woke the unit any more), and what the panel shows
 * with the display off and the backlight on is not known. Any input wakes it, the full view redrawn at once, and
 * acts as usual (a key plays, a knob turns). Fixed: no setting (docs/INTEGRATION.md section 5) */
#define CR_IDLE_MS 180000u                        /* 3 minutes: the logo */
#define CR_SCREEN_OFF_MS 600000u                  /* 10 minutes: the screen off (black) */

static struct {
    uint32_t bheld;                       /* label bits held, as the scan last saw them */
    uint32_t kheld;                       /* key bits held */
    uint8_t armed;                        /* the armed button (tap / hold / layer), NB: none */
    uint8_t open, combo;                  /* its layer (or shift) is open; something else was touched */
    uint8_t hclosed;                      /* the armed button, held again past the hold time, CLOSED this layer (0xFF:
                                           * the loop editor) and is still down: a knob turned now belongs to that
                                           * layer still (cu_knob), not to the view under it */
    uint32_t t0;                          /* when it was pressed (fm1_ms) */
    uint8_t lock;                         /* the open layer (locked since its button's hold), L_NONE: none */
    uint32_t perf_t0;                     /* the PERF layer opened / SELECT turned in it: its picker shows for
                                           * CU_PERF_PICK_MS from then, before the arp band may (cu_arp_layer) */
    uint32_t swallow;                     /* buttons pressed during another's hold: their release does nothing */
    uint8_t oct_chord;                    /* OCT- and OCT+ were down together (panic): no tap */
    uint8_t oct_mod;                      /* OCT- held was a modifier (the editor: + a knob clears its modulation): no tap */
    uint32_t clear_t0;                    /* D#4 down in the loop layer (| 1): CLEAR after 1 s held */
    uint8_t clock;                        /* B3 LOCK: the chord block latches (0.18: kept over a power-off with
                                           * mlatch, in the session: cr_settings.c css_capture / css_apply) */
    uint8_t b3_pend;                      /* B3 is down and nothing else was pressed since: its release is the LOCK
                                           * tap, held past the hold time it opens / closes the LOCK layer */
    uint32_t b3_t0;
    uint16_t kpick;                       /* bit k: chord key k was pressed in the KEY layer as its scale key (MAJ /
                                           * MIN; the others do nothing there): never posted to the engine, its
                                           * release neither latches nor posts (the latch behind the layer is kept) */
    uint8_t mpend;                        /* bit cr_mod_t: Pushed: the only latched extension, pressed again; let
                                           * go alone it goes off (another extension pressed meanwhile: it stays) */
    uint8_t tpend;                        /* bit cr_mod_t: LOCK: the only latched type, pressed again; let go alone
                                           * it goes off (another chord key pressed meanwhile: it stays) */
    uint8_t kpage;                        /* the KEY layer's page (SELECT): 0 Tonic Scale Transpose Single, 1 Key plays
                                           * Keyboard; the first page whenever the layer opens */
    uint8_t lpage;                        /* the PLAY menu's page (SELECT): 0 Length Rate Quantize Count-in, 1 Level;
                                           * the first page whenever the menu opens */
    uint8_t ledit;                        /* the loop editor is open (EDIT in the PLAY menu; cu_ledit_*) */
    uint16_t le_cell;                     /* .. its cursor: the first cell of the selected step */
    uint8_t le_page;                      /* .. its page (SELECT): 0 Step (Note len, Steps, Chance, Velocity), 1 Trig
                                           * (Ratchet, Condition, Voicing); the first whenever it opens */
    uint8_t mlatch, mkill;                /* bit cr_mod_t: latched by LOCK; toggled off by this press (its release
                                           * does nothing) */
    uint32_t kpend;                       /* key bits debounced down and waiting for their velocity (cu_scan_keys) */
    uint32_t kpend_t0[CU_NKEY];           /* .. since (cu_now) */
    uint8_t ctx_vel;                      /* the keys' recent velocity (0: none yet): what a sensed key whose reading
                                           * did not come plays at (docs/VELOCITY.md "the top row") */
    uint8_t last_vel, last_at;            /* the last key's velocity and the aftertouch now (GEEK OUT 2's line) */
    uint8_t key_note[CU_NKEY];            /* root keys (the Full keyboard: every key): note + 1 sent; 0xFF: the
                                           * layer's / a picker's; 0: not pressed, or pressed as a chord key */
    int8_t octave;
    uint8_t opt_open, opt_sel;
    uint8_t page;                         /* PG_* */
    uint32_t last_sound;                  /* the last time a chord sounded or a key was held */
    uint32_t last_input;                  /* the last key, button, knob or MIDI message in (the screensaver, CR_IDLE_MS) */
    uint32_t last_local;                  /* .. the last key, button or knob (not MIDI: what wakes a SysEx screen off) */
    uint32_t sx_local;                    /* last_local when SysEx 74 switched the screen off */
    uint8_t screen_off;                   /* the screen is off (a black frame, cr_ui_draw): nothing drawn */
    uint8_t sx_off;                       /* SysEx 74 SCREEN 0: off until SysEx on or a key, button or knob */
    uint32_t home_t0;                     /* HOME pressed (held CU_HOME_HOLD_MS: the view menu) */
    uint8_t home_hold;                    /* .. armed (pressed alone, the menu not opened yet) */
    struct { uint32_t until; uint8_t kind, col, segs, jump, mark, pool, star, filt; uint16_t pct; char value[8], sub[12 + FELUCCA_PROPHET], label[32], over[16]; } pop;
                                          /* over: the line over the value, in orange (a pool's read-out on the home
                                           * pages: the part's engine). sub: a pool's read-out puts the sound's name
                                           * there, and 12 bytes cut a 12-character name's last ("ENSEMBLE ST", "FOREVER
                                           * KEY"): 13 with PROPHET, whose programs made it plain (FELUCCA_PROPHET 0: the
                                           * image as it was, byte for byte; to be 13 for all when the switch goes) */
                                          /* pool: CU_POOL_ID of a pool's read-out (0: another meter) */
                                          /* jump: the meter's bar jumps to the value (no fill: the picker's preview);
                                           * mark: the square after the name (an overwritten factory preset) */
    uint8_t epk_on, epk_rank;             /* OPT held + PRESETS (1) / + ALGORITHM (2: the bass part's): the engine
                                           * picker, the engine (ENGINE_ORDER rank) */
    uint8_t blvl;                         /* BASS held + ALGORITHM with no layer open: the bass level's shortcut took
                                           * this hold (no layer by the hold time; anything else touched opens it) */
    uint32_t rev_t0;                      /* GLO and SAVE went down together (| 1): held CU_REVERT_MS, the revert */
    uint8_t gsave_pend;                   /* SAVE held while a loop played or a take was open: saved at the stop */
    struct { uint32_t until; uint8_t big, col; char text[24], label[32]; } msg;   /* big: 1 PANIC, 2 red type */
    /* the FM-1 layout's type keys (docs/CHORD-LAYOUT.md; cu_tk_*) */
    uint8_t tk_held;                      /* key bits (CU_TK_MASK): F#3 G#3 A#3 down as chord keys */
    uint8_t tk_ord[3], tk_n;              /* .. in the order pressed (the keys of an open gesture stay until it is decided) */
    uint8_t g_open, g_keys;               /* a gesture is open (not decided): the type keys pressed in it */
    uint32_t g_t0;                        /* .. its first key's press (cu_now) */
    uint8_t tposted;                      /* bit cr_mod_t: the types posted down for the type keys held (LOCK's latched
                                           * ones are cu.mlatch's) */
    uint32_t kwait, kwait_up;             /* root keys waiting for the gesture's decision; .. let go meanwhile */
    uint8_t kwait_vel[CU_NKEY];           /* .. their velocities */
    uint32_t kpick_t0;                    /* the KEY layer's last scale pick (FM-1: the pair's second key ignored) */
    uint32_t kpanel, kmidi;               /* key bits down on the keybed (the scan) / by MIDI Keys; cu.kheld is the press
                                           * as the UI sees it: down while either holds the key */
    uint32_t kdead;                       /* keys down through a layout change: released then; nothing until let go */
    uint8_t full_chk, full_all;           /* Full K Shorts: F#5's chord keys showing, G5's chords on every key */
    uint8_t fsc_was[2];                   /* .. each switch's state before its press (a long press goes back to it) */
    uint32_t fsc_t0[2];                   /* .. its press (0: up) */
} cu;
/* the Full keyboard (KEY layer, Keyboard: Full) with Full K Shorts: F#5 and G5 are its switches, F#5 brings the
 * layout's chord keys back on the low keys (cu.full_chk: the Chords keyboard then, its roots to F5) */
static int cu_fshort(void) { return CU_FN_KEYS && cs.kbd_full && cs.full_short; }
static int cu_full(void) { return cs.kbd_full && !(cu_fshort() && cu.full_chk); }
static uint32_t cu_root0(void) { return cu_full() ? 0u : cu_fm1() ? CU_ROOT0_FM1 : CU_ROOT0; }   /* the first key
                                                    * that plays: a root (the Full keyboard: every key a note) */
static uint32_t cu_kend(void) { return cu_fshort() ? 25u : CU_NKEY; }   /* .. the key after the last (F#5 G5: the
                                                    * shortcuts) */

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
    uint8_t save_step, save_choice;       /* 0 the menu (save_choice 0 Overwrite, 1 Save as new, 2 Favorite: CU_SAVE_FAV,
                                           * 3 Favorites filter, 4 Favorite type), 1 the naming page */
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
static void cu_fx_route_trace(void)               /* the routing as it is (the emulator's tests) */
{
    cu_trace("fx: routing %s %s > %s > %s > %s\n", xfp[XP_RMODE] == 1 ? "chain" : "parallel", cu_fx_posname(0),
             cu_fx_posname(1), cu_fx_posname(2), cu_fx_posname(3));
}
static void cu_rt_mode(int32_t m)                 /* the mode: 0 Parallel, 1 Chain (a routing setting: FX is not
                                                   * switched on) */
{
    xfp[XP_RMODE] = (int16_t)(m ? 1 : 0);
    cu_fx_route_trace();
}
static void cu_rt_release(void)                   /* the held block's key let go: it lands */
{
    if (cu_rt.held)
        cu_trace("fx: routing release %s\n", CU_RT_NAME[(cu_rt.held - 1u) % XFE_N]);
    cu_rt.held = cu_rt.key = 0;
}
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
#define CU_SAVE_FAV 2u                    /* the SAVE menu's third entry: Favorite (the sound's mark; nothing is saved) */
#define CU_SAVE_FILT 3u                   /* .. the fourth: Favorites filter (Off / On: PRESETS turns through favourites) */
#define CU_SAVE_TYPE 4u                   /* .. the fifth: Favorite type (Both / Chord / Bass: where the filter shows it) */
#define CU_SAVE_N 5u
static int cu_save_favonly(uint32_t part);
static void cu_save_close(void);
static void cu_save_commit(void);
static void cu_save_delete(void);
static void cu_pick_begin(void);
static void cu_pick_end(int keep);
static void cu_epk_commit(void);
static uint32_t cu_layer(void);
static uint32_t cu_now(void) { return fm1_ms; }
/* storage_hw.c st_erase (78 PEAK erases_near): an erase the unit starts by itself now would be near playing: a voice
 * of the parts is active, the engine holds a chord or has a note scheduled, or the last input (a key, a button, a
 * knob, a MIDI message: the screensaver's clock) is less than 2 s old */
static inline int cr_erase_near(void)
{
    return cr_parts_busy() || cr_snap.voices || cr_snap.pending || (uint32_t)(cu_now() - cu.last_input) < 2000u;
}

/* the keys' aftertouch, every 5 ms (cr_ui_input and the pump): the highest pressure among the held keys that play
 * (cr_sense.c cr_sense_at), posted to the engine when it changes (CRE_AT: both parts, channel pressure out) */
static void cu_touch_poll(void)
{
#ifdef CR_SENSE_KEYS
    static uint32_t last_ms;
    static uint8_t sent;
    uint32_t now = cu_now(), k, v = 0;
    if (now - last_ms < 5u)
        return;
    last_ms = now;
    if (cs.touch >= CR_TOUCH_VEL_AT) {
        cr_sense_pressure_poll(now, cs.at_sens);
        for (k = 0; k < CU_NKEY; k++)
            if (((CR_SENSE_AT_KEYS >> k) & 1u) && ((cu.kheld >> k) & 1u) && cu.key_note[k] && cu.key_note[k] != 0xFFu &&
                cr_sense_at[k] > v)
                v = cr_sense_at[k];
    }
    if (v != sent) {
        sent = (uint8_t)v;
        cu.last_at = (uint8_t)v;
        cr_post(CRE_AT, 0, 0, (int32_t)v);
        cu_trace("touch: at %u\n", (unsigned)v);
    }
#endif
}
/* the Full keyboard's split point is a KEY: F4, the second F (key 12: F3..E4 left, F4..G5 right; the owner's,
 * 2026-10-11), fixed: the stored split point (cs.split, OPT + KNOB 1) is a pitch class and stays the Chords layout's,
 * untouched (Split Oct and Split Bass there: the roots whose pitch class is below it, in every octave). The engine
 * takes the key as the MIDI note it plays, so it is posted again when the octave moves */
#define CU_SPLIT_FULL 12u                         /* F4 */
static void cu_post_kbd(void)
{
    int32_t n = 53 + (int32_t)CU_SPLIT_FULL + 12 * cu.octave;
    cr_post(CRE_KEYBOARD, (uint32_t)cu_full(), 0, n < 0 ? 0 : n > 127 ? 127 : n);   /* (F#5's chord keys showing: the
                                                    * Chords keyboard) */
    cr_post(CRE_FULLCHORD, 0, 0, cu_fshort() ? (cu.full_all ? 1 : 2) : 0);   /* G5: chords / single notes */
}
static void cu_fsc_enter(void)                    /* the Full keyboard entered (or left): F#5 off, G5 as Key plays
                                                    * makes it (the owner's choice, 2026-10-16: on with Key Mode on and
                                                    * Key plays Chords, else off; not saved) */
{
    cu.full_chk = 0;
    cu.full_all = (uint8_t)(cs.key_on && !cs.key_notes);
    cu.fsc_t0[0] = cu.fsc_t0[1] = 0;
}
static uint32_t cu_scale_bits(void)               /* bit pc: in the key (Key Mode's tonic and scale), as cr_scale_mask */
{
    uint32_t m = cs.scale ? 0x5ADu : 0xAB5u, t = cs.tonic % 12u;
    return ((m << t) | (m >> (12u - t))) & 0xFFFu;
}
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
    cu.pop.star = 0;
    cu.pop.filt = 0;
    cu.pop.pool = 0;
    cu.pop.over[0] = 0;
    cu_cpy(cu.pop.value, value, sizeof cu.pop.value);
    cu_cpy(cu.pop.sub, sub, sizeof cu.pop.sub);
    cu_cpy(cu.pop.label, label, sizeof cu.pop.label);
    cu.pop.col = (uint8_t)col;
    cu.pop.segs = (uint8_t)segs;
    cu.pop.pct = (uint16_t)(hi > lo ? (uint32_t)((v - lo) * 256 / (hi - lo)) : 0u);
}
/* a send's popup (KNOB 4 of the main view, CC 91 / 93 / 94): the per cent of every send read-out (cu_send_pct), big
 * over "%" (as the click level's), the meter 0 .. 100 */
static void cu_send_popup(int32_t v, const char *name, uint32_t col)
{
    char b[8], lb[24];
    cu_int(b, (int32_t)cu_send_pct(v), 0, sizeof b);
    cu_cpy(lb, name, sizeof lb);
    lb[0] = (char)(lb[0] | 0x20);
    cu_popup(b, "%", lb, col, (int32_t)cu_send_pct(v), 0, 100, 12);
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
/* the stripes lit at position pos of a pool of n presets drawn as segs stripes: none on INIT (position 0, not a
 * preset), pos of them, every one on the last preset. A pool longer than its stripes (n > segs = 32): one from preset
 * 01 on, the rest spread over the presets between, every one only on the last */
static uint32_t cu_pool_lit(uint32_t pos, uint32_t n, uint32_t segs)
{
    if (!pos || !n)
        return 0u;
    if (pos >= n)
        return segs;
    return 1u + (pos - 1u) * (segs - 1u) / (n - 1u);
}
/* a pool's meter (mock-up: design/choralroot-fm1-preset-screens.png 1, 5): the position big ("05"), the name (an
 * overwritten factory preset: a square mark after it), a stripe per preset (at most 32; a pool of INIT alone: one, never
 * lit), position k lighting k of them (cu_pool_lit), the bass part's in orange, and two lines that depend on where it
 * is shown.
 * Where OPT held + the part's knob is its engine picker (cu_epk_ok; the owner, 2026-10-11: "show
 * above the preset number in orange the current engine name. Then at the bottom it currently says 'ENGINE - 20/25',
 * let's change that to say 'Hold OPT to change engine', the numbers and the bar already communicate how many presets
 * there are and which one is selected"): the part's engine over the number in orange (the bass with Bass Behaviour
 * Solo: "VA \267 solo"), and under the stripes the way to another engine: CU_POOL_HINT (OPT held + PRESETS for the
 * chord part, OPT held + ALGORITHM for the bass part: cu_epk_step, which works exactly where cu_epk_ok is true, so
 * the line never names a gesture that does nothing there).
 * Anywhere else (the PLAY menu, the editor's picker and its preview, the save dialog; the chord part in the sound
 * editor): nothing over the number and "FM6 \267 05/25" under the stripes (the engine, the position, the pool's
 * presets: INIT, position 00, is not counted; the bass: " \267 solo" after it with Bass Behaviour Solo), as
 * everywhere until 0.18.
 *
 * cu_epk_ok(part): OPT held + PRESETS (part 0) / + ALGORITHM (part 1) is that part's engine picker. The owner,
 * 2026-10-12: "If GLO + Algo is changing bass level anywhere, it should change bass engine, let's keep the UI
 * consistent where we can." The rule: wherever the knob's plain job is the part's sound (its pool), OPT held makes it
 * the part's engine; where the knob has a job of the place's own, OPT changes nothing about it. So:
 *   yes  the home pages (every view), the Options, the layers KEY, PERF, FX, BASS, SEQ (metronome), LOCK and the view
 *        menu (locked open or over the sound editor: their knobs are KNOB 1..4, PRESETS and ALGORITHM stay the
 *        sounds'), and for the BASS part the sound editor's pages (ALGORITHM is the bass sound there; SHIFT is the
 *        editor's for KNOB 1..4 and SELECT only)
 *   no   the PLAY menu (PRESETS the slot, ALGORITHM the record mode) and the loop editor (the step, the voicing; OPT
 *        held: 8 steps, Follow / 0: cu_knob takes those first);
 *        the chord part in the sound editor (PRESETS opens the editor's own picker);
 *        the editor's picker (L_EDIT: EDIT held or PRESETS in the editor) and the save dialog, for both parts: each
 *        holds ONE sound (a preview that OCT- takes back, the sound being named and stored), and another engine
 *        loaded under it by a second picker would be previewed, cancelled or saved in its place. There OPT changes
 *        nothing about either knob, as in 0.18.
 * Until 0.18 OPT + ALGORITHM was the bass level in all of these but the loop editor; 0.18 made it the bass engine on
 * the home pages only (cu_pool_home: no layer, not the editor, not the save dialog) and plain everywhere else */
#define CU_POOL_HINT "Hold OPT to change engine"
static uint32_t cu_layer(void);
static int cu_epk_ok(uint32_t part)
{
    uint32_t l = cu_layer();
    if (l == L_EDIT || l == L_LOOP || (cu.page == PG_SAVE && !l))
        return 0;
    if (!part && cu.page == PG_EDIT && !l)
        return 0;
    return 1;
}
/* ---- favourites (cr_favs.c; docs/PRESETS.md "Favourites"): the mark's key of a sound. A pool entry: a factory preset
 * (the engine, its index in the engine's table; P5: the program's Sequential number), a record that overwrites one
 * (the same key: the same place in the pool), an added record (its slot); INIT: none, 0 */
static int cu_fav_key(uint32_t e, uint32_t kind, uint32_t fk, uint32_t slot, uint32_t *tag, uint32_t *id)
{
    e %= NENGINES;
    if (kind == PK_USER && fk == PF_NONE) {
        *tag = CRF_T_USER;
        *id = slot;
        return slot < UP_SLOTS && up_used(slot);
    }
    if (kind != PK_FACTORY && kind != PK_USER)
        return 0;
    *tag = e;
    *id = fk;
#if FELUCCA_PROPHET
    if (e == ENGI_PROPHET) {
        *id = p5_preset_id(fk);
        return *id != 0u;
    }
#endif
    return 1;
}
static int cu_fav_ent(uint32_t e, pool_ent_t en)   /* the pool entry is marked */
{
    uint32_t tag, id;
    return cu_fav_key(e, en.kind, en.fk, en.slot, &tag, &id) && crf_has(tag, id);
}
/* the part's sound, as it is now: the entry it came from, edited or not (an edited factory sound is its factory
 * origin). A P5 part: the number its program carries, in this build's list or not */
static int cu_fav_part_key(uint32_t part, uint32_t *tag, uint32_t *id)
{
    const track_t *t = &trk[part ? CR_PART_BASS : CR_PART_CHORD];
#if FELUCCA_PROPHET
    if (eng_idx(t->eng_req) == ENGI_PROPHET && !(psnd[part].kind == PK_USER && psnd[part].fk == PF_NONE)) {
        *tag = ENGI_PROPHET;                      /* (a P5 user sound of its own, 0.22: its slot, as any user sound's) */
        *id = psnd[part].kind == PK_INIT ? 0u : p5_patch_id(p5_patch_of(t));
        return *id != 0u;
    }
#endif
    return cu_fav_key(t->eng_req, psnd[part].kind, psnd[part].fk, psnd[part].slot, tag, id);
}
static int cu_fav_part(uint32_t part)
{
    uint32_t tag, id;
    return cu_fav_part_key(part, &tag, &id) && crf_has(tag, id);
}
/* KNOB 4 in the SAVE dialog's Favorite and in the editor's browser: a turn right marks the part's sound, a turn left
 * takes the mark off. 1: the sound can carry a mark (INIT cannot: a message). Written by cr_settings.c crf_poll */
static int cu_fav_turn(uint32_t part, int32_t s)
{
    uint32_t tag, id;
    if (!cu_fav_part_key(part, &tag, &id)) {
        cu_message("INIT: cannot be marked", CR_COL_RED);
        cu_trace("fav: part %u %s cannot be marked\n", (unsigned)part, psnd[part].name);
        return 0;
    }
    (void)crf_set(tag, id, s > 0);
    cu_trace("fav: part %u %s %s (tag %u id %u, %u marked)\n", (unsigned)part, psnd[part].name,
             crf_has(tag, id) ? "on" : "off", (unsigned)tag, (unsigned)id, (unsigned)crf_count());
    return 1;
}
/* KNOB 1 on the SAVE menu's Favorite type: a turn a step through Both / Chord / Bass, stopping at the ends. The type
 * is the sound's, marked or not, and the mark (KNOB 4) never changes it (the owner, 2026-10-13: "favorite/unfavorite
 * them and the type does not change"); a turn here on a sound that is not a favourite also marks it ("I am fine if
 * the sound is unfavorited and you change type to auto favorite it, which would make things quicker to organize"):
 * any turn, one against the end stop too */
static const char *const CU_FAV_TYPE[3] = {"Both", "Chord", "Bass"};
static int cu_fav_type_turn(uint32_t part, int32_t s)
{
    uint32_t tag, id;
    int32_t v;
    if (!cu_fav_part_key(part, &tag, &id)) {
        cu_message("INIT: cannot be marked", CR_COL_RED);
        cu_trace("fav: part %u %s cannot be marked\n", (unsigned)part, psnd[part].name);
        return 0;
    }
    v = (int32_t)crf_type(tag, id) + (s > 0 ? 1 : -1);
    (void)crf_set_type(tag, id, (uint32_t)(v < 0 ? 0 : v > 2 ? 2 : v));
    v = crf_set(tag, id, 1);
    cu_trace("fav: part %u %s type %s%s (tag %u id %u, %u marked)\n", (unsigned)part, psnd[part].name,
             CU_FAV_TYPE[crf_type(tag, id)], v ? ", marked by it" : "", (unsigned)tag, (unsigned)id, (unsigned)crf_count());
    return 1;
}
/* ---- the favourites filter (0.22; the owner, 2026-10-13: "I want filter to reduce the preset list to just scrolling
 * between favorites, still limited by engine"). crf_filter on: PRESETS / ALGORITHM (and the BASS layer's SOUND, the
 * editor's browser) turn through the part's engine's pool places that hold a favourite FITTING THE PART (the chord
 * part: typed Both or Chord; the bass part: Both or Bass), in the pool's own order, INIT left out. An engine with no
 * fitting favourite: its whole pool, as if the filter were off there (cuf_n 0), and the read-out says so. Not
 * filtered: a program change, the session, a loop's recorded sound changes, the Sound Manager (they name a place).
 * cuf[part]: the places as bits, built once per (engine, the marks' generation, the user bank's): a turn never walks
 * the pool (P5: 200 places, each a look through the 32 slots) */
#define cuf crf_x.list                             /* (kept in cr_favs.c's one object: see there why) */
static uint32_t cuf_n(uint32_t part, uint32_t e)   /* the part's fitting favourites in engine e's pool; 0: the filter
                                                    * off, or none (the whole pool is turned) */
{
    uint32_t pos, n, tag, id, i;
    pool_ent_t en;
    part &= 1u;
    e %= NENGINES;
    if (!crf_filter)
        return 0u;
    if (cuf[part].e == e + 1u && cuf[part].gen == crf_gen && cuf[part].upgen == up_gen)
        return cuf[part].n;
    for (i = 0; i < sizeof cuf[0].bits; i++)
        cuf[part].bits[i] = 0;
    cuf[part].n = 0;
    n = pool_count(e);
    for (pos = 1; pos < n && pos < 256u; pos++) {
        en = pool_entry(e, pos);
        if (cu_fav_key(e, en.kind, en.fk, en.slot, &tag, &id) && crf_fits(tag, id, part)) {
            cuf[part].bits[pos / 8u] |= (uint8_t)(1u << (pos % 8u));
            cuf[part].n++;
        }
    }
    cuf[part].e = (uint8_t)(e + 1u);
    cuf[part].gen = crf_gen;
    cuf[part].upgen = up_gen;
    return cuf[part].n;
}
static int cuf_at(uint32_t part, int32_t pos)      /* (after cuf_n) the place holds one of them */
{
    return pos > 0 && pos < 256 && ((cuf[part & 1u].bits[pos / 8] >> (pos % 8)) & 1u);
}
static uint32_t cuf_rank(uint32_t part, uint32_t pos)   /* (after cuf_n) its place among them, 1.., 0: not one */
{
    uint32_t p, r = 0;
    if (!cuf_at(part, (int32_t)pos))
        return 0u;
    for (p = 1; p <= pos; p++)
        r += (uint32_t)cuf_at(part, (int32_t)p);
    return r;
}
/* (after cuf_n > 0) s detents from place cur (-1: the bass OFF) among them. A sound that is not one of them (the
 * filter just turned on, its mark just taken off, a program change): the first detent goes to the nearest favourite
 * in that direction. wrap: round the ends (PRESETS, ALGORITHM), else stopping there (the BASS layer's SOUND);
 * off: OFF is a place below the first (ALGORITHM: past the last comes OFF, as in the whole pool) */
static int32_t cuf_move(uint32_t part, int32_t cur, int32_t s, int wrap, int off)
{
    int32_t k, p, nx;
    for (k = s < 0 ? -s : s; k > 0; k--) {
        nx = cur;
        if (s > 0) {
            for (p = cur + 1; p < 256 && !cuf_at(part, p); p++)
                ;
            if (p < 256)
                nx = p;
            else if (wrap && off && cur >= 0)
                nx = -1;
            else if (wrap)
                for (p = 1; p < 256; p++)
                    if (cuf_at(part, p)) {
                        nx = p;
                        break;
                    }
        } else {
            for (p = cur < 0 ? (wrap ? 255 : 0) : cur - 1; p > 0 && !cuf_at(part, p); p--)
                ;
            if (p > 0)
                nx = p;
            else if (cur < 0)
                nx = cur;
            else if (off)
                nx = -1;
            else if (wrap)
                for (p = 255; p > 0; p--)
                    if (cuf_at(part, p)) {
                        nx = p;
                        break;
                    }
        }
        cur = nx;
    }
    return cur;
}
/* KNOB 1 on the SAVE menu's Favorites filter: right On, left Off; kept with the marks (crf_poll writes it) */
static void cu_filter_turn(int32_t s)
{
    (void)crf_set_filter(s > 0);
    cu_trace("fav: filter %s (chord %u, bass %u of their engines)\n", crf_filter ? "on" : "off",
             (unsigned)cuf_n(0, trk[CR_PART_CHORD].eng_req), (unsigned)cuf_n(1, trk[CR_PART_BASS].eng_req));
}

/* a pool's stripes: one per preset, at most 32; a pool of INIT alone: one */
static uint32_t cu_pool_segs(uint32_t e)
{
    uint32_t n = pool_count(e) - 1u;
    return n > 32u ? 32u : n ? n : 1u;
}
/* which pool a read-out shows (cu.pop.pool, 0: not a pool's meter): the part and its engine. cu_animate moves the
 * stripes of the same pool from where they stood */
#define CU_POOL_ID(part, e) (1u + ((part) ? 1u : 0u) + 2u * (e))
static void cu_pool_popup(uint32_t part, uint32_t pos)
{
    char b[8], nm[13], lb[32], c[8], ov[16], fl[24];
    uint32_t e = trk[part ? CR_PART_BASS : CR_PART_CHORD].eng_req % NENGINES, n = pool_count(e) - 1u;
    uint32_t segs = cu_pool_segs(e), nf = cuf_n(part, e);
    pool_ent_t en = pool_entry(e, pos);
    int home = cu_epk_ok(part);
    cu_2d(b, pos, sizeof b);
    pool_name(e, pos, nm);
    cu_cpy(lb, ENGINES[e]->name, sizeof lb);
    cu_cat(lb, " \267 ", sizeof lb);
    fl[0] = 0;
    if (nf) {                                     /* the filter in force: the place among the part's favourites of this
                                                   * engine ("--": the sound is not one of them), in the star's colour */
        cu_cpy(fl, home ? "favorites " : "fav ", sizeof fl);
        if (cuf_rank(part, pos))
            cu_2d(c, cuf_rank(part, pos), sizeof c);
        else
            cu_cpy(c, "--", sizeof c);
        cu_cat(fl, c, sizeof fl);
        cu_cat(fl, "/", sizeof fl);
        cu_2d(c, nf, sizeof c);
        cu_cat(fl, c, sizeof fl);
        cu_cat(lb, fl, sizeof lb);
    } else if (crf_filter) {                      /* .. on, and no favourite of the part here: the whole pool is turned */
        cu_cpy(fl, "no favorites: all", sizeof fl);
        cu_cat(lb, fl, sizeof lb);
    } else {
        cu_2d(c, pos, sizeof c);
        cu_cat(lb, c, sizeof lb);
        cu_cat(lb, "/", sizeof lb);
        cu_2d(c, n, sizeof c);
        cu_cat(lb, c, sizeof lb);
        if (part && cs.bass_mode == CR_BASS_SOLO) /* Bass Behaviour Solo: the chord part is silent while it is on */
            cu_cat(lb, " \267 solo", sizeof lb);
    }
    cu_cpy(ov, ENGINES[e]->name, sizeof ov);
    if (part && cs.bass_mode == CR_BASS_SOLO)
        cu_cat(ov, " \267 solo", sizeof ov);
    cu_popup(b, nm, !home ? lb : fl[0] ? fl : CU_POOL_HINT, part ? CR_COL_ORANGE : CR_COL_WHITE,
             (int32_t)cu_pool_lit(pos, n, segs), 0, (int32_t)segs, segs);
    cu.pop.filt = (uint8_t)(fl[0] != 0);          /* (the sign that the filter is on: the line under the stripes) */
    if (home)
        cu_cpy(cu.pop.over, ov, sizeof cu.pop.over);
    cu.pop.mark = en.kind == PK_USER && en.fk != PF_NONE;
    cu.pop.star = (uint8_t)cu_fav_ent(e, en);     /* a favourite: the star before its name */
    cu.pop.pool = (uint8_t)CU_POOL_ID(part, e);
    cu_trace("popup: part %u %s / %s%s%s / ", (unsigned)part, b, nm, cu.pop.mark ? " (mark)" : "",
             cu.pop.star ? " (star)" : "");       /* (the place) */
    cu_trace_txt(lb);
    cu_trace("\n");
    if (home) {                                    /* .. and the two lines shown instead of it where OPT picks engines */
        cu_trace("home read-out: part %u over '", (unsigned)part);
        cu_trace_txt(cu.pop.over);
        cu_trace("' line '%s'\n", cu.pop.label);
    }
}
/* PRESETS / ALGORITHM's meter: the part's place in its pool (the bass OFF: "00 off", its pool's stripes all dark:
 * the place before its first, so the stripes of the place it left go out and those of the next come on from none) */
static void cu_sound_popup(int bass)
{
    if (bass && (!cs.bass_sound || !cs.bass_on)) {   /* (BASS tapped off: its sound kept for the next tap) */
        uint32_t e = trk[CR_PART_BASS].eng_req % NENGINES, segs = cu_pool_segs(e);
        cu_popup("00", "off", "bass", CR_COL_ORANGE, 0, 0, (int32_t)segs, segs);
        cu.pop.pool = (uint8_t)CU_POOL_ID(1u, e);
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
static uint32_t cu_layer(void) { return cu.lock; }
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
    if (CU_PAR[p].step > 1 || p == CR_P_SWING)    /* a value (rate, gate, swing, amount): at the knob's speed, snapped
                                                   * with OPT held; the choices one a detent */
        v = cu_kval(cs.par[mode][p], s, CU_PAR[p].min, CU_PAR[p].max, kn.snap);
    else
        v = cs.par[mode][p] + s;
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
#if BOARD_SMK37
#define CRL_FL_BASE ST_SMK_LOOPS                   /* (SMK-37: in USRFLASH, storage.c) */
_Static_assert(2u * CRL_SLOTS <= ST_SMK_LOOP_SECTORS, "SMK: the loop slots outgrow their place");
#else
#define CRL_FL_BASE 0xC8000u
#endif
#define CRL_FL_TYPE 0x4C30u
/* a packed record; also cr_backup.c's staging, where a sound pool bank (ST_PAYLOAD_MAX, 3840 bytes) is the largest */
#define CU_LOOP_BUF (CRL_REC_MAX > 3840u ? CRL_REC_MAX : 3840u)
static uint8_t cu_loop_buf[CU_LOOP_BUF] __attribute__((aligned(4)));
static void cu_ledit_close(void);
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
    st_note(0x100u + k, ST_WHY_LOOP);              /* (3y48: who erased: a loop slot) */
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
    st_note(0x100u + k, ST_WHY_LOOP);
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
 * what was recorded there (docs/LOOPER.md "Slots and the record"). from: the slot it belongs to (cs.loop_slot).
 * Changed since its last save / load (crl.dirty, an
 * overdub in progress): packed (an overdub in progress ends, as the switch would end it), written
 * now when stopped, staged for the stop while it plays (no flash erase then). One staged earlier and still waiting
 * is written first, whatever plays (a second switch while playing: a hiccup rather than a lost take; and the slot
 * about to be read may be that one). Nothing saved when revert loads from itself again (its changes
 * dropped on purpose), in SAFE MODE, after a restore (the flash is newer than the RAM) or without the flash.
 * Returns 1: written now, 0: nothing to write now (or staged), -1: a flash error, the switch refused (the loop stays) */
static int cu_loop_keep(uint32_t k, uint32_t from, int revert, int play)
{
    int need = crl.dirty || crl.cap == CRL_CAP_OD || crl.cap == CRL_CAP_STEP;
    if (cu_loop_flush(play ? "a second switch" : "before a load"))
        return -1;
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
    if (cu_loop_flush("leaving it"))
        return -1;
    return 1;
}
/* a slot -> the loop in RAM: now when stopped, at the end of the cycle when playing; the loop being left goes to its
 * own slot first (cu_loop_keep). revert (power-on, GLO + SAVE): the slot in RAM may be loaded again, its
 * changes dropped; LOOP held + the root of the slot it is on, changed: kept as it is (it is not being left) */
static int cu_loop_load(uint32_t k, int revert)   /* < 0: refused (the message says why), else cu_loop_keep's */
{
    int n, ok = 0, play = cu_playing(), kept;
    uint32_t from = cs.loop_slot;
    char t[24], nb[4];
    cu_loop_gen++;
    if (crl_stage_busy) {
        cu_message("a slot is on its way", CR_COL_RED);
        return -2;
    }
    if (!revert && k == from && (crl.dirty || crl.cap == CRL_CAP_OD || crl.cap == CRL_CAP_STEP)) {
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
/* GLO held + REC (the owner, 2026-10-11: "I would also add shortcut for GLO + REC which changes the record mode,
 * cycling through them"): the next record mode, in the PLAY menu's picker's order, wrapping (the knob stops at the
 * ends), with its name as a message. It only sets the mode, as the knob does: nothing is armed, started or stopped,
 * and while something is captured the mode is taken all the same and counts from the next REC with no capture
 * (cr_loop.c cr_loop_rec reads it only then: the take, the layer, the step entry under way ends as it began) */
static void cu_rec_mode_next(void)
{
    char t[24];
    cs.loop_mode = (uint8_t)((cs.loop_mode % CRL_NMODE + 1u) % CRL_NMODE);
    cr_post(CRE_LOOP, LP_CONF, LC_MODE, cs.loop_mode);
    cu_cpy(t, "Rec: ", sizeof t);
    cu_cat(t, CU_LOOP_MODE[cs.loop_mode], sizeof t);
    cu_message(t, CR_COL_WHITE);
    cu_trace("loop: mode %s (GLO + REC)\n", CU_LOOP_MODE[cs.loop_mode]);
}
static void cu_loop_keys(uint32_t v)               /* GLO tapped in the PLAY menu: the keys play / the slots */
{
    cs.loop_keys = (uint8_t)(v != 0);
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
/* ------------------------------------------------------------------ the loop editor --- */
/* EDIT in the PLAY menu (the owner, 2026-10-11; a step sequencer since 2026-10-12: docs/LOOPER.md "The loop editor"):
 * the loop in RAM as a pattern of sixteenth-note steps (cr_loop.c cr_loop_ed_*: the steps are read from the events,
 * only a step's ratchet, condition, chance and voicing are: crl_data_t.st). Since 2026-10-13 (the owner: "let the
 * preset knob change selected step, select change pages. Algo knob to change the voicing per step ... let the octave
 * keys work"): PRESETS: the step cursor (wrapping; GLO held: 8 steps a detent); SELECT: the page; ALGORITHM: the
 * step's voicing. A chord or a note played on the keys REPLACES the selected step's notes and sounds (the cursor
 * stays); REC clears the step. Page 1 "Step": KNOB 1 how long its notes sound (1/4 1/2 3/4 of a step, 1 .. 64 steps:
 * no other step moves), KNOB 2 the loop's length in steps (1 .. 64; GLO held: to the next of 8 16 32 48 64), KNOB 3
 * its chance, KNOB 4 its velocity. Page 2 "Trig": KNOB 1 its ratchet, KNOB 2 its condition, KNOB 3 its voicing,
 * KNOB 4 Transpose (its notes a semitone a detent). GLO held + the voicing (ALGORITHM, Trig's KNOB 3): Follow / 0.
 * LOOP plays / stops as ever; REC held: the looper's undo; OCT- / OCT+: the octave, as on the view. HOME / EDIT
 * (tapped, or held again) close it, back to the PLAY menu: nothing to confirm, an edit is done when it is made, in
 * RAM, and saved like any loop change (the global save, a slot switch).
 * The view: cu_le, rebuilt from the ISR's loop (IRQ off: two passes over <= 512 events) only when its events change
 * (crl.gen) or the cursor moves; the map (cu_le_map: the page's 64 steps, 104 bytes) is the editor's only buffer. A
 * page is the whole bars that fit 64 steps (cu_le_page): a loop longer than that (a long take) is paged by the cursor */
static crl_edmap_t cu_le_map __attribute__((section(".pool")));
static struct {
    uint16_t gen, c0, nstep;                       /* crl.gen then; the cursor's step; steps */
    uint8_t ok, n, cnt, root, qx, vel;             /* its notes' length (steps); its events, the last one's chord, the loudest */
    uint8_t nf;                                    /* .. its notes are a part of a step: 1 .. 3 quarters (0: n steps) */
    uint8_t ch;                                    /* .. one of its events is a chord (a voicing applies to it) */
    int8_t rat, cond, voic, chance;                /* the step's properties (cr_loop_ed_prop; -1: it has none) */
} cu_le;
static const char *const CU_LE_COND[CRL_NCOND] = {"Always", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4",
                                                  "1st", "not 1st"};
static const char *const CU_LE_RAT[CRL_NRATCH] = {"Off", "x2", "x3", "x4"};
static const char *const CU_LE_FRAC[4] = {"", "1/4", "1/2", "3/4"};
static void cu_le_voic_txt(char *d, uint32_t n)    /* the step's voicing: "Follow", "0", "+3", "-2"; none: "-" */
{
    if (cu_le.voic < 0)
        cu_cpy(d, "-", n);
    else if (!cu_le.voic)
        cu_cpy(d, "Follow", n);
    else
        cu_int(d, (int32_t)cu_le.voic - 13, cu_le.voic != 13, n);
}
static uint32_t cu_le_per(void)                    /* steps a bar (a row of the grid): 16, 12 in 3/4 and 6/8 */
{
    return (cr_snap.lsig % CRL_NSIG == CRL_SIG_44 ? 384u : 288u) / CRL_STEP;
}
static uint32_t cu_le_page(void) { return CRL_STEPS / cu_le_per() * cu_le_per(); }   /* 64; 48 */
static uint64_t cu_le_span(uint32_t a, uint32_t e, uint32_t first, uint32_t n)   /* the steps [a, e) as bits of the
                                                                                 * page's n steps from `first` */
{
    a = a > first ? a : first;
    e = e < first + n ? e : first + n;
    if (e <= a)
        return 0;
    return (e - a >= 64u ? ~(uint64_t)0 : ((uint64_t)1 << (e - a)) - 1u) << (a - first);
}
_Static_assert(CRL_EC_COND == CR_LMC_COND && sizeof ((crl_edmap_t *)0)->cell == sizeof ((cr_screen_t *)0)->lm_cell,
               "crl_edmap_t.cell is cr_screen_t.lm_cell");
static void cu_le_sync(void)
{
    uint32_t c0, n, i, t0, t1, pg = cu_le_page();
    if (!cu.ledit || (cu_le.ok && cu_le.gen == crl.gen && cu_le.c0 == cu.le_cell))
        return;
    fm1_irq_off();
    c0 = (crl.d.len + CRL_STEP - 1u) / CRL_STEP;   /* (the loop made shorter under the cursor: its last step) */
    c0 = cu.le_cell < c0 ? cu.le_cell : c0 ? c0 - 1u : 0u;
    cr_loop_ed_map(&crl, c0 / pg * pg, &cu_le_map);
    cr_loop_ed_cursor(&crl, c0);
    n = cr_loop_ed_nlen(&crl);
    cu_le.nf = (uint8_t)cr_loop_ed_nfrac(&crl);
    cu_le.rat = (int8_t)cr_loop_ed_prop(&crl, CRL_SP_RATCH);
    cu_le.cond = (int8_t)cr_loop_ed_prop(&crl, CRL_SP_COND);
    cu_le.voic = (int8_t)cr_loop_ed_prop(&crl, CRL_SP_VOIC);
    cu_le.chance = (int8_t)cr_loop_ed_prop(&crl, CRL_SP_CHANCE);
    cu_le.cnt = cu_le.vel = cu_le.ch = 0;
    t0 = c0 * CRL_STEP;
    t1 = t0 + CRL_STEP;
    for (i = 0; i < crl.d.nev; i++) {
        const crl_ev_t *e = &crl.d.ev[i];
        if ((e->layer & CRL_HID) || e->t < t0 || e->t >= t1)
            continue;
        cu_le.cnt = (uint8_t)(cu_le.cnt < 255u ? cu_le.cnt + 1u : 255u);
        cu_le.root = e->root;                      /* (the last one written there names the step) */
        cu_le.qx = e->qx;
        if (e->qx >> 4)
            cu_le.ch = 1;
        if (e->vel > cu_le.vel)
            cu_le.vel = e->vel;
    }
    cu_le.gen = crl.gen;
    fm1_irq_on();
    cu.le_cell = cu_le.c0 = (uint16_t)c0;
    cu_le.n = (uint8_t)n;
    cu_le.nstep = cu_le_map.nstep;
    cu_le.ok = 1;
    cu_trace("loopedit: step %u/%u notes %u long %u len %u frac %u ratchet %d cond %d voicing %d chance %d root %u\n",
             (unsigned)c0 + 1u, (unsigned)cu_le.nstep, (unsigned)cu_le.cnt, (unsigned)n, (unsigned)crl.d.len,
             (unsigned)cu_le.nf, (int)cu_le.rat, (int)cu_le.cond, (int)cu_le.voic, (int)cu_le.chance,
             (unsigned)(cu_le.cnt ? cu_le.root : 0u));
}
static void cu_ledit_open(void)
{
    int rc;
    if (crl_stage_busy) {
        cu_message("a slot is on its way", CR_COL_RED);
        return;
    }
    cu_lp.want = 0;
    fm1_irq_off();
    rc = cr_loop_ed_open(&crl);
    fm1_irq_on();
    cu_trace("loopedit: open rc %d\n", rc);
    if (rc != CRL_ED_OK) {                         /* (any loop opens, whatever Quantize says: the grid is 1/16 and
                                                    * nothing is moved until a step is replaced) */
        cu_message("end the take first", CR_COL_RED);
        return;
    }
    cu.ledit = 1;
    cu.le_cell = 0;
    cu.le_page = 0;
    cu_le.ok = 0;
    cu_hot.knob = 0;
}
static void cu_ledit_close(void)
{
    if (!cu.ledit)
        return;
    cu.ledit = 0;
    fm1_irq_off();
    cr_loop_ed_close(&crl, &cr);
    fm1_irq_on();
    cu_hot.knob = 0;
    cu_trace("loopedit: close (events %u len %u)\n", (unsigned)crl.d.nev, (unsigned)crl.d.len);
}
/* SEQ tapped (the owner, 2026-10-11: "The edit sequence is a bit buried, hold Play, then press edit. Most people won't
 * find this ... from Main level you should be able to press the seq key to enter the edit sequence menu", and then:
 * "press once -> sequencer, press SEQ a second time -> Play menu, press SEQ a third time -> Exit"). A cycle of three:
 * the view (no layer, no Options, no page) -> the loop editor, the PLAY menu locked open under it (the editor is that
 * layer's; cu_ledit_open's refusals: then nothing) -> the PLAY menu (the editor closed, however it was opened) -> the
 * view (however the menu was entered; EDIT is the menu's way into the editor). The METRO layer (SEQ held): its own
 * button, the click on / off as before. Every other layer, the Options, the naming page: nothing (the sound editor:
 * its MOD group, cu_tap) */
static void cu_layer_close(void);
static void cu_metro_toggle(void)
{
    cs.metro ^= 1u;
    cr_post(CRE_LOOP, LP_METRO, 0, cs.metro);
    cu_message(cs.metro ? "metronome on" : "metronome off", CR_COL_WHITE);
    cu_trace("metro: %s\n", cs.metro ? "on" : "off");
}
static void cu_seq_tap(void)
{
    if (cu.lock == L_METRO) {
        cu_metro_toggle();
        return;
    }
    if (cu.lock == L_LOOP) {
        if (cu.ledit)
            cu_ledit_close();
        else
            cu_layer_close();
        return;
    }
    if (cu.lock || cu.opt_open || cu.page != PG_NONE)
        return;
    cu_ledit_open();
    if (!cu.ledit)
        return;
    cu.lock = L_LOOP;
    cu.lpage = 0;
    cu_trace("loopedit: from the view\n");
}
static void cu_ledit_move(int32_t s)               /* PRESETS: the cursor, a step a detent (GLO held: 8), wrapping */
{
    int32_t n, c;
    cu_le_sync();
    n = (int32_t)cu_le.nstep;
    if (!n)
        return;
    c = ((int32_t)cu_le.c0 + s) % n;
    cu.le_cell = (uint16_t)(c < 0 ? c + n : c);
    cu_le_sync();
    /* the step preview (GitHub issue 8; the owner, 2026-10-15: "turn preset knob in the step editor, if the step has
     * notes, it plays"): the step the cursor lands on is heard once while the loop is stopped, as the loop would
     * play it, on the loop's voices (cr_loop_ed_preview, in the engine's thread as the keys are); a step without
     * notes ends the one sounding; while the loop plays nothing is struck (the loop is heard). Nothing is written,
     * REC is not armed, the position does not move */
    cr_post(CRE_LOOP, LP_PREVIEW, 0, 0);
    cu_trace("loopedit: preview step %u notes %u %s\n", (unsigned)cu_le.c0 + 1u, (unsigned)cu_le.cnt,
             crl.state == CRL_PLAYING ? "playing" : "stopped");
}
static void cu_ledit_clear(void)                   /* REC: the step's notes go */
{
    int n;
    cu_le_sync();
    fm1_irq_off();
    n = cr_loop_ed_clear(&crl);
    fm1_irq_on();
    cu_trace("loopedit: clear step %u (%d events)\n", (unsigned)cu_le.c0 + 1u, n);
}
/* a property of the selected step (cr_loop_ed_setprop): v clamped to 0 .. top; an empty step has none */
static void cu_ledit_prop(int which, int32_t v, int32_t top, const char *nm)
{
    int rc;
    if (cu_le.rat < 0) {
        cu_trace("loopedit: %s: the step has none\n", nm);
        return;
    }
    v = v < 0 ? 0 : v > top ? top : v;
    fm1_irq_off();
    rc = cr_loop_ed_setprop(&crl, which, (int)v);
    fm1_irq_on();
    if (rc == CRL_ED_NOROOM)
        cu_message("loop too full", CR_COL_RED);
    cu_trace("loopedit: %s %d %s\n", nm, (int)v, rc == 1 ? "ok" : "refused");
}
/* ALGORITHM (every page), Trig's KNOB 3: the step's voicing, a value a detent. snap (GLO held; the owner, 2026-10-11:
 * "hold GLO key and turn voicing to snap to Follow or 0"): between Follow and 0: from either, a turn in either
 * direction gives the other; from any other value clockwise gives 0, the other way Follow. One snap a turn event,
 * however fast the knob (as GLO + Steps) */
static void cu_ledit_voic(int32_t s, int msg, int snap)
{
    char b[24], v[8];
    int32_t to;
    cu_le_sync();
    to = (int32_t)cu_le.voic + s;
    if (snap)
        to = cu_le.voic == 0 ? 13 : cu_le.voic == 13 ? 0 : s > 0 ? 13 : 0;   /* (0 Follow; 13: a rotation of 0) */
    cu_ledit_prop(CRL_SP_VOIC, to, (int32_t)CRL_NVOIC - 1, snap ? "voicing snap" : "voicing");
    if (cu.le_page == 1u)
        cu_hot_set(L_LOOP, 2);
    else if (msg && cu_le.voic >= 0) {             /* (the Step page has no Voicing cell: said) */
        cu_le_sync();
        cu_le_voic_txt(v, sizeof v);
        cu_cpy(b, "step voicing: ", sizeof b);
        cu_cat(b, v, sizeof b);
        cu_message(b, CR_COL_BLUE);
    }
}
/* GLO held + Steps: the loop's length to the next of 8 16 32 48 64 steps in the turn's direction (12 steps a bar, 3/4
 * and 6/8: 12 24 36 48); none further that way: it stays */
static void cu_ledit_snap(int32_t s)
{
    static const uint8_t T16[5] = {8, 16, 32, 48, 64}, T12[4] = {12, 24, 36, 48};
    const uint8_t *T = cu_le_per() == 16u ? T16 : T12;
    uint32_t n = cu_le_per() == 16u ? 5u : 4u, i, nc, v = 0;
    int ok = 0;
    cu_le_sync();
    nc = cu_le.nstep;
    if (s > 0) {
        for (i = 0; i < n && !v; i++)
            if (T[i] > nc)
                v = T[i];
    } else {
        for (i = n; i-- && !v;)
            if (T[i] < nc)
                v = T[i];
    }
    if (v) {
        fm1_irq_off();
        ok = cr_loop_ed_len(&crl, v * CRL_STEP);
        fm1_irq_on();
    }
    cu_trace("loopedit: snap %u steps %s\n", (unsigned)v, ok ? "ok" : "refused");
    cu_hot_set(L_LOOP, 1);
}
/* Trig's KNOB 4, Transpose (the owner, 2026-10-11: "this should move all the notes up or down, so if my chord is C maj
 * and I transpose it up one, now it's a C# maj"): the selected step's notes a semitone a detent, chromatic whatever
 * Key Mode says (an edit of stored notes); a detent that would take a note out of 0 .. 127 is refused, none moves.
 * The cell: the step's root note ("C#4"); while it is hot, how far this turn has moved it ("+2") */
static struct { uint16_t step; int8_t sum; } cu_le_tr;
static int cu_le_tr_hot(void)                      /* the Transpose cell is still hot, on this step */
{
    return cu.le_page && cu_hot.knob == 4u && cu_hot.layer == L_LOOP && (int32_t)(cu_hot.until - fm1_ms) > 0 &&
           cu_le_tr.step == cu_le.c0;
}
static void cu_ledit_transpose(int32_t s)
{
    int32_t d = s;
    int ok = 0;
    if (!cu_le.cnt) {
        cu_trace("loopedit: transpose: the step is empty\n");
        return;
    }
    if (!cu_le_tr_hot()) {
        cu_le_tr.step = cu_le.c0;
        cu_le_tr.sum = 0;
    }
    fm1_irq_off();
    for (; d && !ok; d += d > 0 ? -1 : 1)          /* (a fast turn at the range's end: as far as it goes) */
        if ((ok = cr_loop_ed_transpose(&crl, (int)d)) != 0)
            break;
    fm1_irq_on();
    if (ok)
        cu_le_tr.sum = (int8_t)(cu_le_tr.sum + d);
    else
        cu_message(s > 0 ? "no higher" : "no lower", CR_COL_RED);
    cu_trace("loopedit: transpose %d %s\n", (int)(ok ? d : s), ok ? "ok" : "refused");
}
static void cu_ledit_knob(uint32_t knob, int32_t s)
{
    uint32_t len, nc;
    int ok = 0;
    cu_le_sync();
    nc = cu_le.nstep;
    if (cu.le_page) {                              /* Trig: Ratchet, Condition, Voicing, Transpose */
        if (knob == 0)
            cu_ledit_prop(CRL_SP_RATCH, (int32_t)cu_le.rat + s, (int32_t)CRL_NRATCH - 1, "ratchet");
        else if (knob == 1)
            cu_ledit_prop(CRL_SP_COND, (int32_t)cu_le.cond + s, (int32_t)CRL_NCOND - 1, "condition");
        else if (knob == 2)
            cu_ledit_voic(s, 0, 0);
        else
            cu_ledit_transpose(s);
    } else if (knob == 0) {                        /* how long the step's notes sound: 1/4 1/2 3/4 of a step, then
                                                    * 1 .. 64 steps: the NOTE's length: no other step moves, the loop
                                                    * keeps its length; a long note sounds over the steps after it,
                                                    * which still play. One scale: 0 .. 2 the parts, 3 .. 66 the steps */
        int32_t cur = cu_le.nf ? (int32_t)cu_le.nf - 1 : (int32_t)cu_le.n + 2, v = cur + s;
        if (!cu_le.cnt)
            return;                                /* (an empty step: its cell shows "-") */
        v = v < 0 ? 0 : v > (int32_t)CRL_STEPS + 2 ? (int32_t)CRL_STEPS + 2 : v;
        if (v != cur) {
            fm1_irq_off();
            ok = v < 3 ? cr_loop_ed_setnfrac(&crl, (uint32_t)v + 1u) : cr_loop_ed_setnlen(&crl, (uint32_t)v - 2u);
            fm1_irq_on();
        }
        if (v < 3)
            cu_trace("loopedit: knob 1 note length %s %s\n", CU_LE_FRAC[v + 1], ok ? "ok" : "refused");
        else
            cu_trace("loopedit: knob 1 note length %u %s\n", (unsigned)v - 2u, ok ? "ok" : "refused");
    } else if (knob == 1) {                        /* the loop's length, a step a detent, 1 .. 64 steps: shorter hides
                                                    * the last step (kept until the editor closes: longer brings it
                                                    * back), longer past what there was adds an empty step. A take
                                                    * longer than 64 steps can be made shorter, not longer */
        int32_t top = (int32_t)(nc > CRL_STEPS ? nc : CRL_STEPS), v;
        fm1_irq_off();
        v = (int32_t)nc + s - (s > 0 && crl.d.len % CRL_STEP ? 1 : 0);   /* (a loop played to an odd length: its last
                                                                          * step made whole is the first detent up) */
        v = v < 1 ? 1 : v > top ? top : v;         /* (the only step stays) */
        len = (uint32_t)v * CRL_STEP;
        ok = cr_loop_ed_len(&crl, len);
        fm1_irq_on();
        if (!ok && s > 0)
            cu_message("64 steps at most", CR_COL_RED);
        cu_trace("loopedit: knob 2 loop length %u %s\n", (unsigned)len, ok ? "ok" : "refused");
    } else if (knob == 2) {                        /* the step's chance: clockwise = more (100 % is entry 0) */
        cu_ledit_prop(CRL_SP_CHANCE, (int32_t)cu_le.chance - s, (int32_t)CRL_NCHANCE - 1, "chance");
    } else {                                       /* the step's velocity */
        fm1_irq_off();
        ok = cr_loop_ed_vel(&crl, (int)s * 4);
        fm1_irq_on();
        cu_trace("loopedit: knob 4 velocity %d\n", ok);
    }
    cu_hot_set(L_LOOP, knob);
}
static void cu_ledit_cells(cr_screen_t *s)
{
    static const char *const LB[2][4] = {{"Note len", "Steps", "Chance", "Velocity"}, {"Ratchet", "Condition", "Voicing", "Transpose"}};
    uint32_t k, pg = cu.le_page ? 1u : 0u;
    for (k = 0; k < 4u; k++) {
        if (!LB[pg][k][0])
            continue;
        s->cell[0][k].flags = CR_CF_ON;
        cu_cpy(s->cell[0][k].label, LB[pg][k], sizeof s->cell[0][k].label);
        cu_cpy(s->cell[0][k].value, "-", sizeof s->cell[0][k].value);
    }
    if (pg) {
        if (cu_le.rat >= 0) {
            cu_cpy(s->cell[0][0].value, CU_LE_RAT[cu_le.rat % (int)CRL_NRATCH], sizeof s->cell[0][0].value);
            cu_cpy(s->cell[0][1].value, CU_LE_COND[cu_le.cond % (int)CRL_NCOND], sizeof s->cell[0][1].value);
            cu_le_voic_txt(s->cell[0][2].value, sizeof s->cell[0][2].value);
        } else
            s->cell[0][0].flags = s->cell[0][1].flags = s->cell[0][2].flags = CR_CF_ON | CR_CF_DIM;
        if (cu_le.cnt) {                           /* Transpose: the root note of the step's name; hot: this turn's sum */
            if (cu_le_tr_hot() && cu_le_tr.sum)
                cu_int(s->cell[0][3].value, cu_le_tr.sum, 1, sizeof s->cell[0][3].value);
            else {
                char nn[5];
                cr_note_name(cu_le.root, nn);
                cu_cpy(s->cell[0][3].value, nn, sizeof s->cell[0][3].value);
            }
        } else
            s->cell[0][3].flags = CR_CF_ON | CR_CF_DIM;
        cu_hot_row(s, L_LOOP);
        return;
    }
    cu_int(s->cell[0][1].value, (int32_t)cu_le.nstep, 0, sizeof s->cell[0][1].value);
    if (cu_le.cnt) {
        if (cu_le.nf)
            cu_cpy(s->cell[0][0].value, CU_LE_FRAC[cu_le.nf & 3u], sizeof s->cell[0][0].value);
        else
            cu_int(s->cell[0][0].value, cu_le.n, 0, sizeof s->cell[0][0].value);
        cu_int(s->cell[0][3].value, cu_le.vel, 0, sizeof s->cell[0][3].value);
        s->cell[0][3].glyph = CR_G_BAR;
        s->cell[0][3].pct = (uint8_t)(cu_le.vel * 2u);
    } else
        s->cell[0][0].flags = s->cell[0][3].flags = CR_CF_ON | CR_CF_DIM;
    if (cu_le.chance >= 0) {
        cu_int(s->cell[0][2].value, CRL_CHANCE_PCT((uint32_t)cu_le.chance), 0, sizeof s->cell[0][2].value);
        cu_cat(s->cell[0][2].value, "%", sizeof s->cell[0][2].value);
        s->cell[0][2].glyph = CR_G_BAR;
        s->cell[0][2].pct = (uint8_t)(CRL_CHANCE_PCT((uint32_t)cu_le.chance) * 255u / 100u);
    } else
        s->cell[0][2].flags = CR_CF_ON | CR_CF_DIM;
    cu_hot_row(s, L_LOOP);
}

static int cu_loop_write(void);
/* THE GLOBAL SAVE (SAVE held past the hold time, alone; the owner, 2026-10-11: "Hold Save button to manually do a
 * global save, instead of trying to auto-save settings at runtime. So if you power cycle without global saving it will
 * reset to what it was"): everything about how the instrument is set to play (cr_settings.c cr_settings_commit: the
 * whole settings block and the session) and the loop in RAM if it changed (cu_loop_write), written now; an object
 * that did not change is not written. No confirm: the
 * message "saved" (green) says it happened; a flash error: "save error" (red). Never while a loop plays or a take is
 * open (no flash erase then: the erase stops the audio for ~45 ms an object): "saves at stop", and it is written when
 * the loop next stops (cu_loop_frame). SAFE MODE, a backup just restored: refused with the reason */
static void cu_gsave_now(void)
{
    int rc = 0;
    cu.gsave_pend = 0;
#if CR_HAVE_SETTINGS
    rc = cr_settings_commit();
#endif
    if (cu_loop_write())
        rc = -1;
    cu_message(rc ? "save error" : "saved", rc ? CR_COL_RED : CR_COL_GREEN);
    cu_trace("save: global rc %d\n", rc);
}
static void cu_gsave(void)
{
    if (cr_safe) {
        cu_message("safe mode: no saving", CR_COL_RED);
        return;
    }
    if (cr_restore_lock) {
        cu_message("restored: restart first", CR_COL_RED);
        return;
    }
    if (cu_playing() || cr_snap.lcap != CRL_CAP_NONE) {
        cu.gsave_pend = 1;
        cu_message("saves at stop", CR_COL_RED);
        cu_trace("save: global, at the stop\n");
        return;
    }
    cu_gsave_now();
}
/* the transport's results (the ISR's CRL_DID_*) as messages; a deferred save once the loop stops */
static uint32_t cu_did_seen;
/* The loop in RAM -> its own slot, if it changed since it was loaded or saved (crl.dirty): the GLOBAL SAVE's part for
 * the looper (the owner, 2026-10-11: "Only save the loops after we hold save to manual save, thus if I have some loops
 * recorded that I like and accidentally record over them, I have a way to recover, and also it should make it so you
 * can record / play a loop with no glitches"). Until then a changed loop was written when it stopped and 2 s after a
 * change (cu_loop_autosave, 2026-10-09: gone). The loop is written by this and by a slot switch (cu_loop_keep: "if you
 * switch loop slots, then auto save the loop if it's dirty"), nowhere else: a take, an overdub, an undo, a clear, an
 * edit stay in RAM, and a power cycle or a revert brings the slot's saved loop back. An empty loop (cleared) deletes
 * its slot's record. A loop staged by a switch while it played is written first. 0: written or nothing to write,
 * -1: a flash error */
static int cu_loop_write(void)
{
    uint32_t k = cs.loop_slot, n;
    int rc;
    if (cu_loop_flush("a global save"))
        return -1;
    if (!crl.dirty || crl_stage_busy || cu_lp.q || cr_safe || cr_restore_lock || !crl_fl_ready() || k >= CRL_SLOTS)
        return 0;
    cu_loop_gen++;
    fm1_irq_off();
    n = cr_loop_pack(&crl.d, cu_loop_buf, CRL_REC_MAX);
    fm1_irq_on();
    if (!n && !((cs.loop_used >> k) & 1u)) {       /* empty, and so is its slot: nothing to write */
        fm1_irq_off();
        crl.dirty = 0;
        fm1_irq_on();
        return 0;
    }
    rc = n ? crl_fl_save(k, cu_loop_buf, n) : crl_fl_delete(k);
    cu_trace("loop: save slot %u %u bytes rc %d\n", (unsigned)k + 1u, (unsigned)n, rc);
    if (rc)
        return -1;
    fm1_irq_off();
    crl.dirty = 0;
    fm1_irq_on();
    if (n)
        cs.loop_used |= (uint16_t)(1u << k);
    else
        cs.loop_used &= (uint16_t)~(1u << k);
    return 0;
}

static void cu_loop_frame(void)
{
    static const uint8_t CRL_BARS_UI[CRL_NSYNC] = {0, 1, 2, 4, 8, 16};
    static uint8_t step_no;
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
        else if (d == CRL_DID_STEP && crl.step_len && crl.step_len < CRL_BARS_UI[cs.loop_len % CRL_NSYNC] * cr_loop_bar(&crl)) {
            cu_message(crl.step_len == 1536u ? "Step: 4 bars (64 steps)" : "Step: 5 bars (60 steps)", CR_COL_RED);
            cu_trace("loop: step entry: the Length cut to %u steps\n", (unsigned)(crl.step_len / CRL_STEP));
        }                                          /* (a pattern is 64 steps at most: the Length's first whole bars) */
        else if (cu_layer() == L_LOOP && (d == CRL_DID_ARM || d == CRL_DID_OD_ARM || d == CRL_DID_REP_ARM ||
                                          d == CRL_DID_STEP || d == CRL_DID_REC))
            ;                                      /* (the PLAY menu shows these itself: no box over it) */
        else if (d < sizeof DID / sizeof DID[0])
            cu_message(DID[d], d == CRL_DID_PLAY || d == CRL_DID_STOP ? CR_COL_WHITE : CR_COL_RED);
        cu_trace("loop: %s (state %u layers %u events %u len %u)\n", d < sizeof DID / sizeof DID[0] ? DID[d] : "?",
                 (unsigned)crl.state, (unsigned)crl.d.nlayers, (unsigned)crl.d.nev, (unsigned)crl.d.len);
    }
    if (cr_snap.lstep_no != step_no) {             /* step entry, Length Free: a chord after the 64th step */
        step_no = cr_snap.lstep_no;
        if (cr_snap.lcap == CRL_CAP_STEP) {
            cu_message("64 steps: full", CR_COL_RED);
            cu_trace("loop: step refused (64 steps)\n");
        }
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
    if (cu.ledit && cu_layer() != L_LOOP)          /* the PLAY menu left by any way: the loop editor with it */
        cu_ledit_close();
    if (cu.gsave_pend && !cu_playing() && cr_snap.lcap == CRL_CAP_NONE)
        cu_gsave_now();                            /* SAVE held while it played: now that it stopped */
}

/* -------------------------------------------------------------- actions --- */
static void cu_unlatch(void);
static void cu_tk_quiet(void);
static void cu_latch_toggle(void);
static void cu_quiet(void)                        /* PANIC without its screen: every note off, the loop stopped */
{
    uint32_t k;
    cu_tk_quiet();                                /* (FM-1: no gesture open, no root waiting) */
    cu_unlatch();                                 /* the LOCK latch cleared (LOCK itself stays on) */
    cr_post(CRE_PANIC, 0, 0, 0);
    cu.octave = 0;
    cu_post_kbd();
    for (k = 0; k < CU_NKEY; k++)                 /* the keys still held end nothing more */
        if (cu.key_note[k] && cu.key_note[k] != 0xFFu)
            cu.key_note[k] = 0xFFu;
    cs.sticky = 0;                                /* LATCH off with it (the LED follows); the engine's panic ends */
    cr_post(CRE_STICKY, 0, 0, 0);                 /* the voices but keeps its Sticky flag: told here */
}
static void cu_panic(void)
{
    cu_quiet();
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

/* GLO + SAVE held CU_REVERT_MS (the owner, 2026-10-11: "hold GLO + save to reload the global settings and current
 * preset ... change a bunch of settings then quickly revert back or save"): the unit back to what the last global save
 * wrote, as a power-on brings it: every note off and the loop stopped as PANIC does (without its screen), any layer,
 * page or dialog closed (the engine picker: its preview dropped), then flash's settings and session applied
 * (cr_settings.c cr_settings_revert), then the loop: the slot the settings name (the one selected at that save, as
 * a power-on) loaded from flash again: what was recorded, overdubbed, cleared or edited since is gone, the saved loop
 * is back (empty if the slot holds none); a loop staged by a slot switch while it played is written first (that save
 * is the switch's, not undone). No confirm: the message "reverted". The Options are not the revert's (they are
 * saved by themselves). SAFE MODE (nothing is read there), a backup just restored: refused */
#define CU_REVERT_MS 1000u
static void cu_lock_tap(void);
static void cu_revert(void)
{
    if (cr_safe) {
        cu_message("safe mode: no revert", CR_COL_RED);
        return;
    }
    if (cr_restore_lock) {
        cu_message("restored: restart first", CR_COL_RED);
        return;
    }
    if (cu.lock == L_EDIT)
        cu_pick_end(0);
    if (cu.ledit)
        cu_ledit_close();
    cu_close_all();
    cu.gsave_pend = 0;
    cu.epk_on = 0;
    cu.pop.until = 0;
    if (cu.clock)
        cu_lock_tap();                            /* LOCK off and its latch up (the session's comes back below) */
    cu_quiet();
#if CR_HAVE_SETTINGS
    cr_settings_revert();
#endif
    cr_post(CRE_STICKY, 0, 0, cs.sticky);         /* (after cu_quiet's queued "off": LATCH as saved) */
    cu_loop_conf();                               /* the looper's settings and the metronome -> the ISR */
    cu_loop_flush("before a revert");             /* (a switch's staged save: written, it is not the revert's) */
    fm1_irq_off();                                /* the loop: stopped now (the PANIC is still queued), what it holds
                                                   * dropped, a switch on its way cancelled */
    cr_loop_stop(&crl, &cr);
    crl.dirty = 0;
    crl_stage_busy = 0;
    fm1_irq_on();
    cr_snap.lstate = crl.state;
    cu_lp.q = cu_lp.want = cu_lp.adv_armed = 0;
    cu_loop_load(cs.loop_slot, 1);
    cu_message("reverted", CR_COL_WHITE);
    cu_trace("save: reverted\n");
}

/* PERF tap: perform on / off; FX tap: the effects on / off (with their layer open too: OCT+ there closes the layer as
 * in every layer with nothing to confirm, the owner's rule of 2026-10-10). No message: the PERF / FX LED shows the
 * state (in the layer, its label "perform \267 on" too) */
static void cu_perf_toggle(void)
{
    cs.perform_on ^= 1u;
    cr_post(CRE_PERFORM, 0, 0, cs.perform_on);
    cu_trace("perf: %s\n", cs.perform_on ? "on" : "off");
}
static void cu_fx_toggle(void)
{
    cs.fx_on ^= 1u;
    cu_fx_apply();
    cu_trace("fx: %s\n", cs.fx_on ? "on" : "off");
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
    case BT_PERF:                                 /* perform on / off (its LED lit while on) */
        cu_perf_toggle();
        break;
    case BT_FX:
        cu_fx_toggle();
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
    case BT_LATCH:                                /* Classic: LATCH; FM-1: LOCK on / off (held: its layer; GLO held +
                                                   * LFO: LATCH, cu_btn_press) */
        if (cu_fm1())
            cu_lock_tap();
        else
            cu_latch_toggle();
        break;
    case BT_OPT:
        if (cu.lock == L_LOOP) {                  /* the PLAY menu: GLO toggles its keys (play / the slots): no
                                                   * Options from here; the loop editor: the keys play, nothing */
            if (!cu.ledit)
                cu_loop_keys(!cs.loop_keys);
            break;
        }
        if (cu.lock == L_KEY) {                   /* the KEY layer: GLO toggles its keys (pick / play): no Options
                                                   * from here (OPT held + a knob keeps its second job: a combo) */
            cs.key_play ^= 1u;
            cu_trace("key: keys %s\n", cs.key_play ? "play" : "pick");
            break;
        }
        cu.page = PG_NONE;
        cu.lock = L_NONE;
        cu.opt_open ^= 1u;
        break;
    case BT_EDIT:                                 /* the chord sound's editor (BASS held + EDIT: cu_btn_press) */
        if (cu.lock == L_LOOP) {                  /* the PLAY menu: the loop editor, and out of it */
            if (cu.ledit)
                cu_ledit_close();
            else
                cu_ledit_open();
            break;
        }
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
        else if (cu.page == PG_SAVE && !ce.save_step && ce.save_choice >= CU_SAVE_FAV)
            cu_save_close();                      /* SAVE again on Favorite, the filter, the type: nothing to save, the
                                                   * dialog closes */
        else if (cu.page == PG_SAVE)
            cu_save_commit();                     /* SAVE again: save (as OCT+) */
        else
            cu_save_open(cu.page == PG_EDIT ? ce.part : 0u);
        break;
    case BT_METRO:                                /* SEQ: the loop editor (the click: the PLAY menu's page 2, GLO +
                                                   * SEQ, and SEQ in its own layer) */
        cu_seq_tap();
        break;
    case BT_LOOP:                                 /* play / stop (a take: commit) */
        cr_post(CRE_LOOP, LP_PLAY, 0, 0);
        break;
    case BT_REC:                                  /* record by the mode / end the take */
        if (cu.ledit && cu.lock == L_LOOP)        /* the loop editor: REC clears the step (no recording there) */
            cu_ledit_clear();
        else
            cu_rec();
        break;
    default:
        break;
    }
}

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
    cu.lock = (uint8_t)l;                         /* the layer stays open after release */
    cu_hot.knob = 0;                              /* (a layer opens with no hot cell) */
    if (l == L_KEY && was != L_KEY)               /* the KEY layer opens on its first page */
        cu.kpage = 0;
    if (l == L_LOOP && was != L_LOOP)             /* .. the PLAY menu too */
        cu.lpage = 0;
    if (l == L_PERF && was != L_PERF)             /* the perform layer opens on its picker (the mode's name) */
        cu.perf_t0 = cu_now();
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
    else if (!cs.view_lock)                       /* home already: the next view, unless the view is locked */
        cs.view = (uint8_t)((cs.view + 1u) % V_N);
}

/* HOME held CU_HOME_HOLD_MS (alone): the view menu, a picker (L_VIEW): Chord .. Geek Out 2 by SELECT / the white roots,
 * the last line "Lock: On / Off" (KNOB 1, or the seventh white root, C5); OCT+ ok, OCT- / HOME back. Not over the
 * naming page */
#define CU_HOME_HOLD_MS 600u
static void cu_view_menu(void)
{
    if (cu.page == PG_SAVE)
        return;
    if (cu.lock == L_EDIT)
        cu_pick_end(0);
    cu.opt_open = 0;
    if (cu.page != PG_EDIT)
        cu.page = PG_NONE;
    cu.lock = L_VIEW;
    cu_hot.knob = 0;
    cu_trace("view: menu (%s, lock %s)\n", CU_VIEW[cs.view % V_N], cs.view_lock ? "on" : "off");
}

static void cu_calib_start(void);
static void cu_lock_tap(void);
/* Options > Reset Settings, confirmed (cr_settings.c cr_settings_reset: what is reset and what is kept). The session
 * ends as a PANIC ends it, without its screen: every note off, a playing loop stopped (kept; a take being recorded
 * is dropped, as PANIC drops it), LATCH off, the octave and the voicing back to 0; and LOCK off with its latch (LOCK
 * and its latch would otherwise come back with the session). Back on the view, with the message */
static void cu_reset(void)
{
    cu_reset_ask = 0;
    cu_close_all();
    if (cu.clock)
        cu_lock_tap();
    cu_quiet();
    cr_post(CRE_VOICING, 0, 0, -(int32_t)cr_snap.voicing);
#if CR_HAVE_SETTINGS
    cr_settings_reset();
#endif
    cu.opt_sel = O_STYLE;
    cu_message(cr_c1_off && cs.dualcore && !CR_SINGLE_CORE ? "settings reset \267 restart" : "settings reset", CR_COL_GREEN);   /* (Dual Core
                                                   * was Off at this power-on: On, the default since 0.22, is saved,
                                                   * the core starts at the next; but not in a power-on that kept
                                                   * core 1 off after a crash beside it: Off is saved, cr_settings.c
                                                   * crs_dc_bar) */
    cu_trace("options: settings reset%s\n", cr_c1_off && cs.dualcore && !CR_SINGLE_CORE ? " (Dual Core starts at the restart)" : "");
}
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
    if (b == B_OCTUP && cu.opt_open && cu.opt_sel == O_RESET && !cu.lock && !cu.page) {
        if (!cu_reset_ask) {
            cu_reset_ask = 1;                     /* the first OCT+: "OCT+ again" (OCT-, another entry, leaving the
                                                   * Options: nothing happens, cu_options_screen / cr_build_screen) */
            return;
        }
        cu_reset();                               /* the second: every option to its default, saved */
        return;
    }
    if (b == B_OCTUP && cu.opt_open && cu.opt_sel == O_CALIB && !cu.lock && !cu.page) {
        cu_close_all();
        cu_calib_start();                         /* Options > Calibrate, OCT+: Felucca's HARDWARE CALIBRATION */
        return;
    }
    if (cu.ledit && cu_layer() == L_LOOP) {       /* the loop editor: the octave, as on the view (the owner,
                                                   * 2026-10-13; they closed it until then: HOME and EDIT do): the
                                                   * notes entered next are entered there */
        cu.octave = (int8_t)(cu.octave + (b == B_OCTUP ? 1 : -1));
        cu.octave = (int8_t)(cu.octave < -2 ? -2 : cu.octave > 2 ? 2 : cu.octave);
        cu_post_kbd();
        cu_popup_num(cu.octave, 1, "", "octave", CR_COL_WHITE, -2, 2, 5);
        cu_trace("loopedit: octave %d\n", (int)cu.octave);
        return;
    }
    if (cr_snap.lcap == CRL_CAP_STEP && (!cu_layer() || cu_layer() == L_LOOP) && !cu.opt_open && cu.page == PG_NONE) {
        cr_post(CRE_LOOP, LP_STEP, 0, b == B_OCTUP ? CRL_STEP_REST : CRL_STEP_BACK);   /* step entry: a rest / back */
        cu_trace("loop: step %s\n", b == B_OCTUP ? "rest" : "back");
        return;
    }
    if (cu_layer()) {                             /* a layer: OCT- back, OCT+ confirms where there is something to
                                                   * confirm, else it closes too (the rule for every layer) */
        uint32_t l = cu_layer();
        if (l == L_EDIT) {                        /* the engine picker: OCT+ keeps the sound, OCT- cancels */
            cu_pick_end(b == B_OCTUP);
            cu_layer_close();
        } else if (cu.lock)                         /* KEY PERF FX BASS METRO, the PLAY menu (step entry: above), the
                                                   * view menu: nothing to confirm, both close (on / off is the
                                                   * layer's own button, tapped) */
            cu_layer_close();
        return;
    }
    if (cu_picker_ctx()) {                        /* OCT+: OK (a picker's change is live already), OCT-: back */
        cu_close_all();
        return;
    }
    cu.octave = (int8_t)(cu.octave + (b == B_OCTUP ? 1 : -1));
    cu.octave = (int8_t)(cu.octave < -2 ? -2 : cu.octave > 2 ? 2 : cu.octave);
    cu_post_kbd();                                /* (the split key's note moves with it) */
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
#if FELUCCA_PROPHET
    if (eng_idx(t->eng_req) == ENGI_PROPHET)
        p5_leave(t);                              /* (the mix is kept: not a UNISON that a P5 program gave the part) */
#endif
    cu_drive_load(t, e, en->npresets ? cu_preset_drive(&en->presets[p0]) : 0);   /* (FM TONE: its first preset's) */
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
    if (cuf_n(part, e))                            /* the favourites filter: the part's favourites of this engine */
        pos = (uint32_t)cuf_move(part, (int32_t)cu_part_pos(part), s, 1, 0);
    else
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
    cu_trace("picker: open part %u %s crc %04x\n", (unsigned)cpk.part, cpk.snd.name, (unsigned)cu_snd_crc(t));
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
#if FELUCCA_PROPHET
        if (eng_idx(t->eng_req) == ENGI_PROPHET)
            p5_voice_restore(t, cpk.p[P_VOICE]);  /* (the load took the factory program's UNISON: the snapshot's mode) */
#endif
        if (!cpk.part) {
            cs.fx_on = cpk.fx_on;
            for (i = 0; i < CU_NFX; i++)
                cs.fx_amt[i] = cpk.fx_amt[i];
            cu_fx_apply();                        /* (the track's: the amounts' again, whatever the snapshot held) */
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
 * the pool's 128 slots (upool.c, 0.22; 32 records before) (cr_bank.c cb_store writes the binding) */
#define CU_SAVE_FOOT CU_K1 " change \267 " CU_K2 " filter \267 " CU_K3 " type \267 " CU_K4 " favorite"   /* (KNOB 1 .. 4: spelled out they are wider
                                                                                * than the line) */
static const char *const CU_SAVE_NAME[CU_SAVE_N] = {"overwrite", "save as new", "favorite", "filter", "type"};   /* (the traces) */
/* the part's engine has no store for its sound: its menu has the three favourites' entries alone, and OCT+ is refused.
 * Until 0.22 that was P5 (a record of the 32 held the part's parameters and no program); the sound pool (upool.c) keeps
 * a P5 sound's whole program with its record, so P5 saves as every engine does (docs/PROPHET.md "Saving") */
static int cu_save_favonly(uint32_t part)
{
#if FELUCCA_PROPHET && !UP_POOL
    return eng_idx(trk[part ? CR_PART_BASS : CR_PART_CHORD].eng_req) == ENGI_PROPHET;
#else
    (void)part;
    return 0;
#endif
}
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
    if (cu_save_favonly(part))                     /* P5: the dialog is its Favorite alone (cu_save_favonly) */
        ce.save_choice = CU_SAVE_FAV;
    ce.del_ask = 0;
    cu.opt_open = 0;
    cu.lock = L_NONE;
    cu.page = PG_SAVE;
    cu.pop.until = 0;                              /* (a PRESETS meter would hide the dialog) */
    cu_trace("save: dialog part %u %s%s (%s)\n", (unsigned)part, psnd[part].name, psnd[part].edited ? "*" : "",
             CU_SAVE_NAME[ce.save_choice]);
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
    char l[UP_LABEL], nm[13];
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
    char name[16], l[UP_LABEL];
    int rc;
    if (ce.save_step == 0u && ce.save_choice >= CU_SAVE_FAV) {   /* Favorite, the filter, the type: OCT+ saves nothing,
                                                                  * writes no slot */
#if !UP_POOL
        if (cu_save_favonly(part)) {                /* (before the sound pool: P5 had no store) */
            cu_message("P5: no save yet", CR_COL_RED);
            cu_trace("save: part %u P5 refused (no store)\n", (unsigned)part);
            return;
        }
#endif
        cu_trace("save: favorite, nothing to save\n");
        (void)part;
        return;
    }
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
    char l[UP_LABEL], b[24], pl[24];
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
        cu.perf_t0 = cu_now();                    /* SELECT turned (a detent at the list's end too): the picker, for
                                                   * CU_PERF_PICK_MS from the last detent */
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
    case L_VIEW:
        if (i < (int32_t)V_N) {
            cs.view = (uint8_t)i;
            cu_trace("view: %s\n", CU_VIEW[i]);
        } else if (i == (int32_t)V_N) {
            cs.view_lock ^= 1u;
            cu_trace("view: lock %s\n", cs.view_lock ? "on" : "off");
        }
        break;
    case L_FX:                                    /* SELECT: a page (Reverb 1, Reverb 2, Chorus, Delay 1, ..) */
        if (i < (int32_t)CU_NFXP) {
            if ((int32_t)cu_fx_pg() != i)
                cu_hot.knob = 0;                  /* another page: its row has no hot cell */
            cs.fx_page = CU_FXP[i].page;
            if (cs.fx_page == CU_FXPG_SENDS) {    /* (the Sends page: cs.fx_sel stays, KNOB 4 of the main view's) */
                cu_trace("fx: sends\n");
                break;
            }
            if (cs.fx_page == CU_FXPG_FILTER) {   /* (the Filter page: the same) */
                cu_trace("fx: filter\n");
                break;
            }
            if (cs.fx_page == CU_FXPG_ROUTE) {    /* (the Routing page: the same) */
                cu_fx_route_trace();
                break;
            }
            cs.fx_sel = CU_FXP[i].fx;
            cu_trace("fx: effect %s %u/%u\n", CU_FX[cs.fx_sel].name, (unsigned)cs.fx_page + 1u, (unsigned)CU_FXP[i].of);
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
    case L_LOOP:                                   /* KNOB 1: the length (the next take's) */
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
    case L_FX: return (int32_t)cu_fx_pg();
    case L_BASS: return cs.bass_mode;
    case L_EDIT: return cu_engine_rank(cu_edit_trk()->eng_req % NENGINES);
    case L_LOOP: return cs.loop_len;
    case L_METRO: return cs.metro_sig;
    case L_VIEW: return cs.view;
    default: return 0;
    }
}
static int32_t cu_layer_count(uint32_t l)
{
    uint32_t n;
    switch (l) {
    case L_PERF: return 7;
    case L_FX: return CU_NFXP;
    case L_BASS: return 4;
    case L_EDIT: cu_engine_at(0, &n); return (int32_t)n;
    case L_LOOP: return (int32_t)CRL_NSYNC;
    case L_METRO: return CRL_NSIG;
    case L_VIEW: return V_N;
    default: return 0;
    }
}

/* the KEY layer takes the chord block (the one layer where it does not keep its job: the owner's call, 2026-10-10):
 * MAJ and MIN pick the scale at the press, LOCK on or off, held or tapped; the other six chord keys do nothing there.
 * None of them reaches the engine or the LOCK latch (cu.kpick), so the chord latched behind the layer is what it was
 * when the layer closes. B3 stays LOCK. A chord key held since before the layer keeps its own release.
 * That is the layer's "keys: pick" (the default). OPT (GLO) tapped in the layer switches to "keys: play" and back
 * (cs.key_play, saved: settings v13): the roots and the chord block play exactly as outside the layer, so the key is
 * heard while KNOB 1..4 change it (as the PLAY menu's Keys, docs/LOOPER-MODES.md) */
#define CU_KEY_LOCK 6u                             /* B3 */
#define CU_KEY_MIN 3u                              /* G#3 (CU_MOD_OF_KEY) */
#define CU_KEY_MAJ 5u                              /* A#3 */
static int cu_key_pick(void) { return cu_layer() == L_KEY && !cs.key_play; }   /* (keys: play: the chord block's own job) */
static void cu_key_scale(uint32_t k)               /* a chord key pressed in the KEY layer */
{
    if (k != CU_KEY_MIN && k != CU_KEY_MAJ)
        return;
    cs.scale = k == CU_KEY_MIN ? CR_SCALE_MINOR : CR_SCALE_MAJOR;
    cu_post_key();
    cu_hot_set(L_KEY, 1);                          /* (the Scale cell turns hot, as under KNOB 2) */
    cu_trace("key: %s scale %s\n", k == CU_KEY_MIN ? "MIN" : "MAJ", cs.scale ? "minor" : "major");
}

static void cu_layer_key(uint32_t l, uint32_t k)   /* a root key while layer l is open */
{
    int32_t wi = cu_white_idx(k);
    if (l == L_FX) {                               /* (only the Routing page owns them: cu_layer_keys) */
        int32_t b = cu_rt_block_of(k);
        if (b == (int32_t)XFE_N)                   /* A#4: the mode */
            cu_rt_mode(xfp[XP_RMODE] != 1);
        else if (b >= 0 && !cu_rt.held) {          /* a block's key: held, it lifts (one at a time) */
            cu_rt.held = (uint8_t)(b + 1);
            cu_rt.key = (uint8_t)(k + 1u);
            cu_trace("fx: routing hold %s\n", CU_RT_NAME[b]);
        }
        return;
    }
    if (k < CU_PICK0)                              /* (FM-1's C4, C#4: no job in the pick maps, the owner 2026-10-16) */
        return;
    if (l == L_KEY) {                              /* the tonic, Key Mode on; MIN / MAJ held with it: that scale too
                                                    * (held from before the layer as well); neither: the scale stays
                                                    * (so MIN tapped, then a root, is a minor key: until 2026-10-10 a
                                                    * root alone meant major) */
        cs.tonic = (uint8_t)((53u + k) % 12u);
        if ((cu.kheld >> CU_KEY_MIN) & 1u)
            cs.scale = CR_SCALE_MINOR;
        else if ((cu.kheld >> CU_KEY_MAJ) & 1u)
            cs.scale = CR_SCALE_MAJOR;
        cs.key_on = 1;
        cu_post_key();
        cu_trace("key: root tonic %s %s\n", CU_NOTE[cs.tonic], cs.scale ? "minor" : "major");
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
    if (l == L_LOOP && wi >= 0) {                  /* the white roots: slots 1..10 */
        if (wi < (int32_t)CRL_SLOTS)
            cu_loop_load((uint32_t)wi, 0);
        return;
    }
    cu_layer_pick(l, wi);
}

static void cu_layer_knob(uint32_t l, uint32_t knob, int32_t s)   /* KNOB 1..4 (0..3) while layer l is open */
{
    int32_t v;
    switch (l) {
    case L_KEY:
        if (cu.kpage) {                            /* page 2: KEY PLAYS, KEYBOARD, SNAP, NUMERALS */
            if (knob == 0) {
                cs.key_notes = (uint8_t)(s > 0);
                cr_post(CRE_KEYPLAYS, 0, 0, cs.key_notes);
                cu_trace("key: knob 1 key plays %s\n", cs.key_notes ? "notes" : "chords");
            } else if (knob == 1) {
                cs.kbd_full = (uint8_t)(s > 0);
                cu_fsc_enter();                    /* (Full K Shorts: F#5 / G5 as they start) */
                cu_post_kbd();
                cu_trace("key: knob 2 keyboard %s\n", cs.kbd_full ? "full" : "chords");
            } else if (knob == 2) {                /* SNAP: Off / Scale / Chord (a root out of the scale, with Key plays
                                                    * Notes or the Full keyboard: the engine, cr_snap_note) */
                v = (int32_t)cs.snap + s;
                cs.snap = (uint8_t)(v < 0 ? 0 : v > 2 ? 2 : v);
                cr_post(CRE_KEYSNAP, 0, 0, cs.snap);
                cu_trace("key: knob 3 snap %s\n", CU_SNAP[cs.snap]);
            } else {                               /* NUMERALS: Off / On (the chord's Roman numeral on the Chord view) /
                                                    * Solfege (the same degree as a syllable; display only) */
                v = (int32_t)cs.numerals + s;
                cs.numerals = (uint8_t)(v < 0 ? 0 : v > 2 ? 2 : v);
                cu_trace("key: knob 4 numerals %s\n", CU_NUMS_TR[cs.numerals]);
            }
            cu_hot_set(L_KEY, knob);
            break;
        }
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
            v = kn.snap ? kn_snap_rng(cs.transpose, s, -24, 24, 12, 0) : cs.transpose + s;   /* (a semitone a detent;
                                                    * OPT held: octaves) */
            cs.transpose = (int8_t)(v < -24 ? -24 : v > 24 ? 24 : v);
            cr_post(CRE_TRANSPOSE, 0, 0, cs.transpose);
            cu_trace("key: knob 3 transpose %d\n", (int)cs.transpose);
        } else {                                   /* SINGLE NOTES: Full Octave / Split Oct / Split Bass */
            v = (int32_t)cs.single + s;
            cs.single = (uint8_t)(v < 0 ? 0 : v > 2 ? 2 : v);
            cu_post_single();
            cu_trace("key: knob 4 single %s\n", cs.single == 2u ? "split bass" : cs.single ? "split" : "full");
        }
        cu_hot_set(L_KEY, knob);                  /* (no popup: the knob row is the readout, its cell turns hot) */
        break;
    case L_LOCK:                                   /* KNOB 1: a type key clears / keeps the latched extensions;
                                                    * KNOB 2: the extension keys toggle / are pushed */
        if (knob == 0) {
            cs.lock_keep = (uint8_t)(s > 0);
            cu_trace("lock: type key %s\n", cs.lock_keep ? "keeps" : "clears");
        } else if (knob == 1) {
            cs.lock_push = (uint8_t)(s > 0);
            cu_trace("lock: ext keys %s\n", cs.lock_push ? "pushed" : "toggle");
        } else
            break;
        cu_hot_set(L_LOCK, knob);
        break;
    case L_VIEW:                                   /* KNOB 1: the lock */
        if (knob == 0 && cs.view_lock != (uint8_t)(s > 0)) {
            cs.view_lock = (uint8_t)(s > 0);
            cu_trace("view: lock %s\n", cs.view_lock ? "on" : "off");
        }
        break;
    case L_PERF:
        cu_param_turn(cu_perf_mode(), CU_PERF_KNOB[cu_perf_mode()][knob], s, 0);
        cu_hot_set(L_PERF, knob);                 /* (no popup: the cell turns hot) */
        break;
    case L_FX: {
        uint32_t pg = cu_fx_pg();
        int32_t c0 = CU_FXP[pg].c[knob & 3u], id, lo, hi, e = cu_fx_send_of(pg, c0);
        int16_t *q;
        char lb[14];
        cu_cpy(lb, CU_FXP[pg].label[knob & 3u], sizeof lb);
        lb[0] = (char)(lb[0] | 0x20);
        if (cs.fx_page == CU_FXPG_ROUTE) {        /* the Routing page: KNOB 1 moves the held block, KNOB 2 the mode */
            if (knob == 1u && ((s > 0) != (xfp[XP_RMODE] == 1)))
                cu_rt_mode(s > 0);
            else if (knob == 0u && cu_rt.held && cu_fx_move(cu_rt.held - 1u, s))
                cu_fx_route_trace();
            break;
        }
        if (c0 == FC_NONE)
            break;
        if (e >= 0) {                             /* a send: an effect's "Send" cell, a cell of the Sends page: the
                                                   * one step (cu_send_step, the sound editor's for the same knob) */
            uint32_t sid = CU_FX[e].send;
            v = cu_send(0, sid);
            cu_send_set(0, sid, cu_send_step(v, s, kn.snap));
            cu_hot_set(L_FX, knob);               /* (no popup: the knob row is the readout, its cell turns hot) */
            cu_trace("fx: knob %u %s send %u\n", (unsigned)knob + 1u, CU_FX[e].name, (unsigned)cu_send(0, sid));
            break;
        }
        id = cu_fx_id(c0);
        if (id >= FC_G) {                         /* the chorus's (song.g) */
            q = &song.g[id - FC_G];
            lo = GP[id - FC_G].min;
            hi = GP[id - FC_G].max;
        } else {                                  /* the delay's, the reverb's (cr_fx.c: the audio ISR reads them, a
                                                   * block at a time, and glides) */
            q = &xfp[id];
            lo = XFP[id].min;
            hi = XFP[id].max;
        }
        if (c0 == FC_TIME)                        /* Time, synced or free: clockwise is FASTER echoes (a shorter time, a
                                                   * smaller division; the owner 2026-10-11). The knob alone is
                                                   * turned round: the stored value, the read-out and the glyph's
                                                   * spacing are the time's as before */
            s = -s;
        v = hi - lo > 20 ? cu_kval(*q, s, lo, hi, kn.snap) : *q + s;   /* (a value at the knob's speed / snapped; a
                                                   * division, a ratio, a switch: one a detent) */
        v = v < lo ? lo : v > hi ? hi : v;
        if (((id == XP_FLOW || id == XP_FHIGH) && v != *q) || (id == XP_FON && v)) {   /* the master filter: a cutoff
                                                   * turned, or Filter to On, switches the filter and FX on (as a
                                                   * send turned switches FX on: the knob is heard) */
            xfp[XP_FON] = 1;
            if (!cs.fx_on) {
                cs.fx_on = 1;
                cu_fx_apply();
            }
        }
        *q = (int16_t)v;
        cu_hot_set(L_FX, knob);
        cu_trace("fx: knob %u %s %s %d\n", (unsigned)knob + 1u, cu_fx_pgname(), lb, (int)*q);
        break;
    }
    case L_BASS:
        if (knob == 0) {                           /* BEHAVIOUR */
            v = cs.bass_mode + (s > 0 ? 1 : -1);
            cu_layer_pick(L_BASS, v < 0 ? 0 : v > 3 ? 3 : v);
        } else if (knob == 1) {                    /* REGISTER */
            cr_post(CRE_BASS_VOICING, 0, 0, s);
            cu_trace("bass: knob 2 register %+d\n", (int)s);
        } else if (knob == 2) {                    /* SOUND */
            v = (int32_t)cs.bass_sound + s;
            if (cuf_n(1, trk[CR_PART_BASS].eng_req))   /* the favourites filter: off, then the bass part's favourites */
                v = cuf_move(1, (int32_t)cs.bass_sound - 1, s, 0, 1) + 1;
            cu_bass_go((uint32_t)(v < 0 ? 0 : v));
            cu_trace("bass: knob 3 sound %u\n", (unsigned)cs.bass_sound);
        } else {                                   /* LEVEL */
            trk[CR_PART_BASS].p[P_LEVEL] = (int16_t)cu_kval(trk[CR_PART_BASS].p[P_LEVEL], s, 0, 127, kn.snap);
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
        } else if (knob == 3) {                    /* KNOB 4: the favourite's mark on the sound under the cursor (the
                                                    * one previewed), right on, left off; the browser stays (0.22, the
                                                    * owner 2026-10-13); the read-out shows it with its star */
            if (cu_fav_turn(ce.part, s)) {
                cu_pool_popup(ce.part, cu_part_pos(ce.part));
                cu.pop.jump = 1;
            }
        }                                          /* (KNOB 3: nothing; KNOB 4 had no job in 0.19 .. 0.21. Until 0.18 KNOB 4 switched the white roots
                                                    * between choosing engines and playing; the owner, 2026-10-12: "I
                                                    * don't see any case in which I would want to scroll through presets
                                                    * and not be able to immediately play them": they always play, SELECT
                                                    * steps the engines) */
        break;
    case L_LOOP:                                   /* the PLAY menu. Page 1: LENGTH RATE QUANTIZE COUNT-IN; page 2
                                                    * (SELECT): LEVEL METRONOME. The loop editor: STEP, its LENGTH, the loop's
                                                    * STEPS, the step's VELOCITY */
        if (cu.ledit) {
            cu_ledit_knob(knob, s);
            break;
        }
        if (cu.lpage) {
            if (knob == 1) {                       /* the click on / off (SEQ's tap until 0.18: the owner, 2026-10-11:
                                                    * "Add Metronome to the second param page of the play menu") */
                cs.metro = s > 0;
                cr_post(CRE_LOOP, LP_METRO, 0, cs.metro);
                cu_trace("loop: knob 2 metronome %s\n", cs.metro ? "on" : "off");
            } else if (knob != 0)
                break;
            else {
                v = cs.loop_level + s * 5;
                cs.loop_level = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
                cr_post(CRE_LOOP, LP_CONF, LC_LEVEL, cs.loop_level);
                cu_trace("loop: knob 1 level %u\n", (unsigned)cs.loop_level);
            }
        } else if (knob == 0) {                    /* the length (the next take's) */
            v = cs.loop_len + s;
            cu_layer_pick(L_LOOP, v < 0 ? 0 : v >= (int32_t)CRL_NSYNC ? (int32_t)CRL_NSYNC - 1 : v);
        } else if (knob == 1) {                    /* the Rate: taken at the loop's start while it plays (cr_loop.c) */
            v = cs.loop_rate + s;
            cs.loop_rate = (uint8_t)(v < 0 ? 0 : v >= (int32_t)CRL_NRATE ? CRL_NRATE - 1u : (uint32_t)v);
            cr_post(CRE_LOOP, LP_CONF, LC_RATE, cs.loop_rate);
            cu_trace("loop: knob 2 rate %s\n", CU_RATE[cs.loop_rate]);
        } else if (knob == 2) {
            if (cs.loop_mode % CRL_NMODE == CRL_MODE_STEP) {   /* (the setting is the takes': kept as it is) */
                cu_message("Step: always 1/16", CR_COL_RED);
                cu_trace("loop: knob 3 rests (Step: 1/16)\n");
                break;
            }
            v = cs.loop_quant + s;
            cs.loop_quant = (uint8_t)(v < 0 ? 0 : v >= (int32_t)CRL_NQUANT ? CRL_NQUANT - 1u : (uint32_t)v);
            cr_post(CRE_LOOP, LP_CONF, LC_QUANT, cs.loop_quant);
            cu_trace("loop: knob 3 quantize %s\n", CU_QUANT[cs.loop_quant]);
        } else {
            cs.loop_count_in = s > 0;
            cr_post(CRE_LOOP, LP_CONF, LC_COUNTIN, cs.loop_count_in);
            cu_trace("loop: knob 4 count-in %s\n", cs.loop_count_in ? "on" : "off");
        }
        cu_hot_set(L_LOOP, knob);                 /* (no popup: the cell turns hot) */
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
    cu.last_input = cu.last_local = cu_now();     /* (the screensaver's clock) */
    cu.home_hold = 0;                             /* HOME held with something else: no view menu */
    if (cu.armed != NB) {                         /* something touched during a hold: a combo, the layer at once */
        cu.combo = 1;
        if (!cu.open || cu.blvl) {                /* (.. after BASS + ALGORITHM's level too: cu_bass_level_hold) */
            cu.open = 1;
            cu.blvl = 0;
            if (cu.armed == BT_METRO && cu.ledit && cu.lock == L_LOOP)
                return;                           /* the loop editor: SEQ is its way out, no METRO layer from there
                                                   * (SEQ held + a knob: the knob stays the editor's) */
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
 * bottom-row key (6 m7 M7 9) toggles its extension. LOCK off and PANIC clear the latch.
 * The LOCK layer (B3 held; the owner, 2026-10-10) has two styles for it, both acting only while LOCK is on:
 *   Type key  Clears (the above) / Keeps: a type key replaces the latched type and leaves the latched extensions
 *   Ext keys  Toggle (the above) / Pushed: the extension keys are a set as the type keys are: an extension key
 *             pressed with no chord key held starts a new set of that key alone (keys pressed while it is held
 *             join, as ever), the type untouched. One exception: the set is exactly one key and that key is
 *             pushed again alone: it goes off when let go (the way to no extension). Keeps + Pushed: a type key
 *             changes the type only, an extension key the extensions only
 * The engine (cr_engine.h cr_mod_lock, the owner's report 2026-10-12): with LOCK on the keys posted down are this
 * latch's set and nothing else: Advanced and Free make the roots held follow it (no toggling of their own on top),
 * Free sounds the last root again while a chord key is physically held (cu_mod_touch: CRE_MODTOUCH after a press's
 * events, before a release's). A set replaced in one push goes as one change: the ups quiet, then the down */
#define CU_MOD_TOP ((1u << CR_MOD_DIM) | (1u << CR_MOD_MIN) | (1u << CR_MOD_MAJ) | (1u << CR_MOD_SUS))
static uint32_t cu_mods_phys(void)                 /* bit cr_mod_t: the chord keys physically held */
{
#if BOARD_SMK37
    uint32_t k, m = 0;                             /* (the SMK-37: their pads; Classic's table) */
    for (k = 0; k < CU_ROOT0; k++)
        if (cu_mod_of(k) != 0xFFu && (((uint32_t)(cu_pheld & ~cu_ppick) >> k) & 1u))
            m |= 1u << cu_mod_of(k);
    return m;
#else
    uint32_t k, m = 0;
    for (k = 0; k < 9u; k++)
        if (cu_mod_of(k) != 0xFFu && !cu.key_note[k] &&      /* (not the KEY layer's, nor a Full-keyboard note) */
            !(cu_fm1() && ((CU_TK_MASK >> k) & 1u)) &&    /* (FM-1's type keys: the types they resolved to, below) */
            (((cu.kheld & ~(uint32_t)cu.kpick) >> k) & 1u))
            m |= 1u << cu_mod_of(k);
    return m | (cu_fm1() ? cu.tposted : 0u);
#endif
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
static void cu_mod_touch(void)                     /* LOCK: the chord keys physically held, to the engine */
{
    if (cu.clock)
        cr_post(CRE_MODTOUCH, 0, 0, (int32_t)cu_mods_phys());
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
    cu.mkill = cu.mpend = cu.tpend = 0;
}
static void cu_lock_menu(void)                     /* B3 held past the hold time: the LOCK layer, or out of it */
{
    if (cu.page == PG_SAVE)                        /* (not over the naming page, as the view menu) */
        return;
    if (cu.lock == L_LOCK) {
        cu_layer_close();
        return;
    }
    if (cu.lock == L_EDIT)
        cu_pick_end(1);
    cu.opt_open = 0;
    if (cu.page != PG_EDIT)
        cu.page = PG_NONE;
    cu.lock = L_LOCK;
    cu_hot.knob = 0;
    cu_trace("layer: open %u (locked)\n", (unsigned)L_LOCK);
}
static void cu_lock_tap(void)                      /* B3 */
{
    cu.clock ^= 1u;
    if (!cu.clock) {
        cr_post(CRE_MODLOCK, 0, 0, 0);             /* (first: the ups are plain releases then: a chord held keeps
                                                    * its spec in Advanced / Free, as it always did at LOCK off) */
        cu_unlatch();
    } else {
        cr_post(CRE_MODLOCK, 0, 0, 1);
        cu_mod_touch();
    }
    cu_trace("lock: %s\n", cu.clock ? "on" : "off");
}
static void cu_mod_press(uint32_t k)               /* a chord key down (Classic's, FM-1's extension keys) */
{
    uint32_t m = cu_mod_of(k), bit = 1u << m, x;
    uint32_t others = cu_mods_phys() & ~bit;
    if (!(bit & CU_MOD_TOP) && cu.mpend != bit)    /* a second extension key in the push: no lone repeat any more */
        cu.mpend = 0;
    if (cu.tpend != bit)                           /* any other chord key in a type key's push: neither */
        cu.tpend = 0;
    if (cu.clock && !others && cu.mlatch) {        /* nothing else held, a latch: reset / toggle */
        uint32_t keep = 0xFFu;
        if ((bit & CU_MOD_TOP) && (cu.mlatch & CU_MOD_TOP) == bit)   /* the ONLY type on, pushed again: if it is let
                                                    * go alone it goes off (cu_mod_release: no chord type, the roots
                                                    * play single notes; the owner's rule, 2026-10-11, whatever the
                                                    * two styles say). The latched extensions are not touched: they
                                                    * stay lit, do nothing without a type and apply again with one */
            cu.tpend = (uint8_t)bit;
        else if (bit & CU_MOD_TOP)                 /* a type key: the latch is that type alone; "Type key: Keeps"
                                                    * (cs.lock_keep): the latched extensions stay. No type latched
                                                    * (it was pushed off): the type comes back to the extensions
                                                    * waiting there, Clears or Keeps */
            keep = bit | (cs.lock_keep || !(cu.mlatch & CU_MOD_TOP) ? ~CU_MOD_TOP & 0xFFu : 0u);
        else if (cs.lock_push) {                   /* "Ext keys: Pushed": a new set of extensions, this key alone
                                                    * (the type stays); keys pressed while it is held join it */
            if ((cu.mlatch & ~CU_MOD_TOP) == bit)  /* .. but the ONLY extension on, pushed again: if it is let go
                                                    * alone it goes off (cu_mod_release: the one way to "no
                                                    * extension" in Pushed, the owner's rule); it sounds on until
                                                    * then, and another extension pressed meanwhile joins it */
                cu.mpend = (uint8_t)bit;
            else
                keep = bit | CU_MOD_TOP;
        } else if (cu.mlatch & bit) {              /* Toggle: a latched extension off */
            cu.mlatch &= (uint8_t)~bit;
            cu.mkill |= (uint8_t)bit;
            cr_post(CRE_MOD, m, 0, 0);
            cu_mod_touch();
            cu_mods_trace();
            return;
        }
        for (x = 0; x < CR_NMOD; x++)
            if (((cu.mlatch & ~keep) >> x) & 1u)
                cr_post(CRE_MOD, x, 1, 0);         /* (quiet: the set changes once, at the down below) */
        cu.mlatch &= (uint8_t)keep;                /* (a kept key is already down since it was latched) */
    }
    cr_post(CRE_MOD, m, 0, 1);                     /* (a second down changes nothing; LOCK: the voices follow here) */
    cu_mod_touch();
}
static void cu_mod_release(uint32_t k)
{
    uint32_t m = cu_mod_of(k), bit = 1u << m;
    cu_mod_touch();                                /* (first: Free's re-sounded chord ends with the last key held, on
                                                    * the set it had; then the set changes) */
    if (cu.mkill & bit) {                          /* toggled off by this press */
        cu.mkill &= (uint8_t)~bit;
        return;
    }
    if ((cu.tpend | cu.mpend) & bit) {             /* the only type on (Pushed: the only extension on) was pushed
                                                    * again, alone: off */
        cu.mpend &= (uint8_t)~bit;
        cu.tpend &= (uint8_t)~bit;
        if (cu.clock) {
            cu.mlatch &= (uint8_t)~bit;
            cr_post(CRE_MOD, m, 0, 0);
            cu_mods_trace();
            return;
        }
    }
    if (cu.clock) {                                /* LOCK: stays down */
        cu.mlatch |= (uint8_t)bit;
        cu_mods_trace();
        return;
    }
    cr_post(CRE_MOD, m, 0, 0);
}

/* The FM-1 layout's type keys (docs/CHORD-LAYOUT.md "The combo table", "The combine window"). F#3 G#3 A#3 are DIM MIN
 * MAJ, and G#3 + A#3 held together are SUS, always; with Secret Chords applying (All, or Simple in Play Style Simple:
 * the engine's crx_secret_quality) F# + G# is m add4 (MIN + SUS), F# + A# augmented (MAJ + SUS), all three the power
 * chord (DIM + SUS); without, those pairs are "last pressed wins" (the pair counts as pressed when it became a pair).
 * The engine keeps its model: the UI resolves the keys held into the cr_mod_t set Classic's keys would give for that
 * chord (cu_tk_resolve) and posts the difference (cu_tk_post: the new types HELD, the old ones up, then one PRESS of the
 * last: one re-type, and the engine's secret rule names the chord). A key of the pair let go leaves the other HELD
 * (no press: what sounds stays, the next root takes it).
 * The gesture (the owner, 2026-10-16, rounds 2 and 3 and question 16): a type key that can combine (G#, A#; F# while
 * secret applies), pressed with none of those held, opens a gesture; the type keys pressed while it is open join it,
 * and nothing is posted until it is decided: at the window's close (Options > SUS Window, cs.suswin ms after its first
 * key), or as soon as no further key could change it (the pair with secret not applying, all three with it). Then the
 * set changes once (Advanced / Free re-type a held root once, LOCK's latch once), and the roots pressed meanwhile
 * (cu.kwait: held back by cu_key_press) start, with the decided chord. A root with no type key pushed, or with type keys
 * held from a gesture decided before, plays at once. No chord is ever changed in place */
#define CU_TKF 1u                                  /* F#3 */
#define CU_TKG 3u                                  /* G#3 */
#define CU_TKA 5u                                  /* A#3 */
static int cu_secret_on(void)                      /* the engine's crx_secret_quality scope */
{
    return cs.secret == CR_SECRET_ALL || (cs.secret == CR_SECRET_SIMPLE && cs.playstyle == CR_PS_SIMPLE);
}
static uint32_t cu_tk_comb(void)                   /* key bits of the type keys that can combine now */
{
    return (1u << CU_TKG) | (1u << CU_TKA) | (cu_secret_on() ? 1u << CU_TKF : 0u);
}
static int32_t cu_tk_pos(uint32_t k)
{
    uint32_t i;
    for (i = 0; i < cu.tk_n; i++)
        if (cu.tk_ord[i] == k)
            return (int32_t)i;
    return -1;
}
static void cu_tk_prune(void)                      /* the order: the keys held only */
{
    uint32_t i, n = 0;
    for (i = 0; i < cu.tk_n; i++)
        if ((cu.tk_held >> cu.tk_ord[i]) & 1u)
            cu.tk_ord[n++] = cu.tk_ord[i];
    cu.tk_n = (uint8_t)n;
}
/* the type keys t (key bits) -> the types to post, the one that counts as pressed last at the end; returns how many */
static uint32_t cu_tk_resolve(uint32_t t, uint8_t *w)
{
    uint32_t f = (t >> CU_TKF) & 1u, g = (t >> CU_TKG) & 1u, a = (t >> CU_TKA) & 1u, sec = (uint32_t)cu_secret_on();
    if (g && a) {
        if (!f) {
            w[0] = CR_MOD_SUS;
            return 1;
        }
        if (!sec && cu_tk_pos(CU_TKF) > cu_tk_pos(CU_TKG) && cu_tk_pos(CU_TKF) > cu_tk_pos(CU_TKA)) {
            w[0] = CR_MOD_SUS;                     /* (F# after the pair: DIM is the last pressed) */
            w[1] = CR_MOD_DIM;
        } else {
            w[0] = CR_MOD_DIM;                     /* (secret: the power chord; else the pair is the last pressed) */
            w[1] = CR_MOD_SUS;
        }
        return 2;
    }
    if (f && (g || a)) {
        uint8_t o = (uint8_t)(g ? CR_MOD_MIN : CR_MOD_MAJ);
        if (sec) {                                 /* m add4, augmented: the type and SUS (the engine's secret) */
            w[0] = o;
            w[1] = CR_MOD_SUS;
        } else if (cu_tk_pos(CU_TKF) > cu_tk_pos(g ? CU_TKG : CU_TKA)) {
            w[0] = o;                              /* last pressed wins */
            w[1] = CR_MOD_DIM;
        } else {
            w[0] = CR_MOD_DIM;
            w[1] = o;
        }
        return 2;
    }
    if (f | g | a) {
        w[0] = (uint8_t)(f ? CR_MOD_DIM : g ? CR_MOD_MIN : CR_MOD_MAJ);
        return 1;
    }
    return 0;
}
/* the types w[0..n) to the engine against what the held keys posted (cu.tposted; LOCK's latched ones are cu.mlatch):
 * the new ones HELD, the ones gone up (and out of the latch), then, press, one PRESS of the last (or force: the set
 * was changed by the caller already) */
static void cu_tk_post(const uint8_t *w, uint32_t n, int press, int force)
{
    uint32_t i, wb = 0, nd, up, m;
    for (i = 0; i < n; i++)
        wb |= 1u << w[i];
    nd = wb & ~(uint32_t)(cu.tposted | cu.mlatch);
    up = cu.tposted & ~wb;
    for (i = 0; i < n; i++)
        if ((nd >> w[i]) & 1u)
            cr_post(CRE_MOD, w[i], 1, CR_MOD_HELD);
    for (m = 0; m < CR_NMOD; m++)
        if ((up >> m) & 1u)
            cr_post(CRE_MOD, m, 1, CR_MOD_UP);
    cu.mlatch &= (uint8_t)~up;
    cu.tposted = (uint8_t)wb;
    if (press && n && (nd || up || force))
        cr_post(CRE_MOD, w[n - 1u], 0, CR_MOD_PRESS);
    cu_trace("chord keys: types 0x%x%s\n", (unsigned)wb, press ? " (press)" : "");
}
static void cu_tk_flush(void)                      /* the roots that waited for the decision: they start now */
{
    uint32_t k;
    for (k = 0; k < CU_NKEY; k++) {
        uint32_t bit = 1u << k;
        if (!(cu.kwait & bit))
            continue;
        if (cu.key_note[k] && cu.key_note[k] != 0xFFu) {
            cr_post(CRE_KEY, cu.key_note[k] - 1u, cu.kwait_vel[k], 1);
            if (cu.kwait_up & bit) {               /* (let go meanwhile: a short chord, the stab kept) */
                cr_post(CRE_KEY, cu.key_note[k] - 1u, 0, 0);
                cu.key_note[k] = 0;
            }
        }
        cu_trace("chord keys: key %u plays after the window\n", (unsigned)k);
    }
    cu.kwait = cu.kwait_up = 0;
}
static void cu_tk_lock_release(void)               /* LOCK: the last type key let go */
{
    uint32_t m, last = 0;
    if (cu.tpend & CU_MOD_TOP) {                   /* the same set pushed again, alone: off (its extensions stay) */
        for (m = 0; m < CR_NMOD; m++)
            if ((cu.tpend >> m) & 1u)
                last = m;
        for (m = 0; m < CR_NMOD; m++)
            if ((cu.tpend >> m) & 1u)
                cr_post(CRE_MOD, m, m != last, CR_MOD_UP);
        cu.mlatch &= (uint8_t)~cu.tpend;
        cu.tposted &= (uint8_t)~cu.tpend;
        cu.tpend = 0;
    }
    cu.mlatch |= cu.tposted;                       /* (the rest: latched, down as they are) */
    cu.tposted = 0;
    cu_mods_trace();
}
static void cu_tk_decide(void)                     /* the gesture decided: its set posted once, the roots start */
{
    uint8_t w[2];
    uint32_t n = cu_tk_resolve((uint32_t)cu.tk_held | cu.g_keys, w), x;
    cu.g_open = 0;
    if (cu.clock) {                                /* LOCK: the set as one type key (cu_mod_press's rules) */
        uint32_t d = 0, lt = cu.mlatch & CU_MOD_TOP, ext = cu_mods_phys() & ~CU_MOD_TOP & 0xFFu;
        int force = 0;
        for (x = 0; x < n; x++)
            d |= 1u << w[x];
        if (!ext && cu.mlatch) {
            if (lt == d)                           /* the same set again: it goes off when let go */
                cu.tpend = (uint8_t)d;
            else {                                 /* another set: the latch is that type (Keeps: with its extensions) */
                uint32_t keep = d | (cs.lock_keep || !lt ? ~CU_MOD_TOP & 0xFFu : 0u);
                for (x = 0; x < CR_NMOD; x++)
                    if (((cu.mlatch & ~keep) >> x) & 1u) {
                        cr_post(CRE_MOD, x, 1, CR_MOD_UP);
                        force = 1;
                    }
                cu.mlatch &= (uint8_t)keep;
            }
        }
        cu_tk_post(w, n, 1, force);
    } else
        cu_tk_post(w, n, 1, 0);
    if (cu.g_keys & ~cu.tk_held) {                 /* gesture keys let go before the decision */
        cu_tk_prune();
        if (cu.clock) {
            if (!cu.tk_held)
                cu_tk_lock_release();
        } else {
            n = cu_tk_resolve(cu.tk_held, w);
            cu_tk_post(w, n, 0, 0);
        }
    }
    cu.g_keys = 0;
    cu_mod_touch();
    cu_mods_trace();
    cu_tk_flush();
}
static void cu_tk_press(uint32_t k)                /* an FM-1 type key down (as a chord key) */
{
    uint8_t w[2];
    uint32_t bit = 1u << k, n, t;
    if (cu.tk_held & bit)
        return;
    cu.tk_held |= (uint8_t)bit;
    if (cu_tk_pos(k) < 0 && cu.tk_n < 3u)
        cu.tk_ord[cu.tk_n++] = (uint8_t)k;
    if (cu.g_open) {                               /* joins the open gesture: decided early if nothing else can */
        cu.g_keys |= (uint8_t)bit;
        t = cu.tk_held | cu.g_keys;
        if (cu_secret_on() ? (t & CU_TK_MASK) == CU_TK_MASK : ((t >> CU_TKG) & (t >> CU_TKA) & 1u))
            cu_tk_decide();
        return;
    }
    if ((cu_tk_comb() >> k) & 1u && !(cu.tk_held & ~bit & cu_tk_comb())) {   /* a new gesture: nothing yet */
        cu.g_open = 1;
        cu.g_t0 = cu_now();
        cu.g_keys = (uint8_t)bit;
        cu_trace("chord keys: gesture opens (key %u)\n", (unsigned)k);
        return;
    }
    if (cu.tk_held == bit) {                       /* alone, and it cannot combine (F# with Secret Chords not applying):
                                                    * a gesture of one key, decided at once (LOCK: it replaces the latch
                                                    * as any type key; until 3y164 it was added to it) */
        cu.g_keys = (uint8_t)bit;
        cu_tk_decide();
        return;
    }
    cu.tpend = 0;                                  /* (joins keys held from a decided gesture: at once) */
    n = cu_tk_resolve(cu.tk_held, w);
    cu_tk_post(w, n, 1, 0);
    cu_mod_touch();
}
static void cu_tk_release(uint32_t k)
{
    uint8_t w[2];
    uint32_t bit = 1u << k, n;
    if (!(cu.tk_held & bit))
        return;
    cu.tk_held &= (uint8_t)~bit;
    if (cu.g_open)                                 /* (counted at the decision) */
        return;
    cu_tk_prune();
    if (cu.clock) {                                /* LOCK: the set is let go as one key */
        cu_mod_touch();
        if (!cu.tk_held)
            cu_tk_lock_release();
        return;
    }
    n = cu_tk_resolve(cu.tk_held, w);              /* the rest HELD, no press: what sounds stays */
    cu_tk_post(w, n, 0, 0);
}
static void cu_tk_poll(void)                       /* the scan: the window closed */
{
    if (cu.g_open && cu_now() - cu.g_t0 >= (uint32_t)cs.suswin)
        cu_tk_decide();
}
static uint32_t cu_tk_keys_of(uint32_t m)          /* latched types (bit cr_mod_t) -> the FM-1 type keys that show
                                                    * them: SUS the pair, the secret chords their keys (MIN + SUS F# G#,
                                                    * MAJ + SUS F# A#, DIM + SUS all three), a type its key */
{
    uint32_t sus = (m >> CR_MOD_SUS) & 1u, k = 0;
    if (sus)
        k |= (m & ((1u << CR_MOD_DIM) | (1u << CR_MOD_MIN) | (1u << CR_MOD_MAJ))) ? 1u << CU_TKF : (1u << CU_TKG) | (1u << CU_TKA);
    if ((m >> CR_MOD_DIM) & 1u)
        k |= (1u << CU_TKF) | (sus ? (1u << CU_TKG) | (1u << CU_TKA) : 0u);
    if ((m >> CR_MOD_MIN) & 1u)
        k |= 1u << CU_TKG;
    if ((m >> CR_MOD_MAJ) & 1u)
        k |= 1u << CU_TKA;
    return k;
}
static void cu_tk_quiet(void)                      /* PANIC, a layout change: no gesture, no root waiting */
{
    cu.g_open = 0;
    cu.g_keys = 0;
    cu_tk_prune();
    cu.kwait = cu.kwait_up = 0;
}

/* the root keys belong to layer l (its map), else they play: PERF and FX play the chord so a mode or an effect is
 * heard at once (SELECT picks there: docs/PRESETS.md "Also in this pass"), the engine picker plays the preview
 * (always, since 0.18: its roots chose engines until then) */
static int cu_layer_keys(uint32_t l)
{                                                  /* (the Routing page owns only its five black keys, cu_rt_key:
                                                    * the rest play, so an effect is heard while it is routed) */
    return l && l != L_PERF && l != L_FX && l != L_LOCK && l != L_EDIT &&
           !(l == L_LOOP && (!cs.loop_keys || cu.ledit)) && !(l == L_KEY && cs.key_play);
}   /* (the PLAY menu with Keys = Play, the loop editor, the KEY layer with keys: play: they play, docs/LOOPER-MODES.md) */

#ifndef CU_KEY_T0
#define CU_KEY_T0(k) fm1_in.note_t0[k]             /* the first scan that saw key k closed (hal/fm1_input.h) */
#endif
/* Key velocity (cr_sense.c; docs/VELOCITY.md). With Options > Key Touch Dynamic (or Velocity + Aftertouch), a key with
 * the converter (CR_SENSE_KEYS) that plays a note waits, once debounced, for its velocity (cu_scan_keys: cu.kpend;
 * 8.8 ms after the touch sample, 13.2 with the probe on, CU_VEL_WAIT_MS at most) and plays at it, as measured (the level's law is the voice's:
 * mod.c cr_touch_gain); a key without it plays at the keys' recent velocity (cu.ctx_vel), else at Options >
 * Velocity. Fixed: every key at Options > Velocity, as before. */
#ifndef CR_SENSE_KEYS                              /* (the emulator, the tests: no analog rows) */
#define CR_SENSE_KEYS 0u
static volatile uint16_t cr_sense_land[CU_NKEY];
static uint32_t cr_sense_take(uint32_t k) { (void)k; return 0; }
#endif
#define CU_VEL_WAIT_MS 18u                         /* the velocity is known four samples after the touch sample: 8.8 ms at
                                                    * 2.2 ms a sample, 13.2 with the top row's probe on (a third frame
                                                    * phase: every 3.3 ms), the touch sample up to one sample after the
                                                    * contact, the debounce 1.1 .. 2.2 ms after it: 15.4 ms at most */
static uint8_t cu_press_vel;                       /* the pending key's velocity, for cu_key_press (0: none) */
#define CU_KEY_SENSED(k) (cs.touch && ((CR_SENSE_KEYS >> (k)) & 1u) && (k) >= cu_root0())
static uint32_t cu_key_vel(uint32_t k)
{
    uint32_t v = cu_press_vel;
    (void)k;
    if (!cs.touch)
        return cs.vel;                             /* Key Touch Fixed: Options > Velocity, as before */
#if BOARD_SMK37
    v = fm1_in.key_vel[SMK_KEY_F3 + k];            /* SMK-37 (Dynamic): the key's two contacts and its factory curve
                                                    * (smk_input.h), known when the note starts */
#endif
    if (!v)
        v = cu.ctx_vel;                            /* (no reading in time: the keys' recent velocity) */
    if (!v)
        return cs.vel;
    return v < 1u ? 1u : v > 127u ? 127u : v;
}
static void cu_fsc_press(uint32_t i);              /* Full K Shorts: F#5, G5 (below) */
static void cu_fsc_release(uint32_t i);
static uint32_t cu_cb_end(void) { return cu_fm1() ? CU_ROOT0_FM1 : CU_ROOT0; }   /* the chord block: the keys below */
static void cu_key_press(uint32_t k)
{
    uint32_t l;
    cu.kheld |= 1u << k;
    cu.last_sound = cu_now();
    if (cu_fm1() && cu.armed == BT_LATCH && !cu.open && !cu.combo && !(cu.page == PG_EDIT && !cu_layer())) {
        cu.open = 1;                               /* FM-1: LFO down (before the hold time) and a key: LOCK's tap now,
                                                    * in the order played, as B3's in Classic (no layer from this press) */
        cu_lock_tap();
    }
    cu_activity();
    if (cu.b3_pend && k != CU_KEY_LOCK) {          /* B3 still down and another key: LOCK's tap now, in the order
                                                    * played (no layer from this press) */
        cu.b3_pend = 0;
        cu_lock_tap();
    }
    if (k >= 25u && cu_fshort() && !cu_key_pick()) {   /* Full K Shorts: F#5 the chord keys, G5 chords on every key */
        cu_fsc_press(k - 25u);
        cu.key_note[k] = 0xFFu;
        return;
    }
    if (CU_FN_KEYS && k == CU_KEY_LOCK && !cu_fm1() && !cu_full()) {   /* B3: a tap (its release) toggles LOCK, a hold
                                                    * is its layer (Classic; FM-1: B3 is the 9, LOCK is on LFO) */
        cu.b3_pend = 1;
        cu.b3_t0 = cu_now();
        return;
    }
    /* the Full keyboard (cu_full: cs.kbd_full, F#5's chord keys not showing): the keys of the chord block are notes as the roots are (below), B3 too: no
     * chord key, no LOCK, no LOCK layer; the latch behind it (cu.mlatch, down in the engine, which ignores it in this
     * mode) is what it was when the mode is left. One place keeps them: the KEY layer with its keys on pick, which
     * owns the keys (MAJ / MIN the scale, a root the tonic; B3 nothing there) */
    if (CU_FN_KEYS && k < cu_cb_end() && (!cu_full() || cu_key_pick())) {   /* the chord block keeps its job in every layer but KEY's, where */
        if (cu_key_pick()) {                       /* MAJ / MIN pick the scale (cu_key_scale); B3 is LOCK everywhere */
            uint32_t other = (k == CU_KEY_MIN ? 1u << CU_KEY_MAJ : k == CU_KEY_MAJ ? 1u << CU_KEY_MIN : 0u);
            cu.kpick |= (uint16_t)(1u << k);
            if (cu_fm1() && (cu.kpick & other) && cu_now() - cu.kpick_t0 < (uint32_t)cs.suswin)
                cu_trace("key: %s ignored (the pair's second key)\n", k == CU_KEY_MIN ? "MIN" : "MAJ");
            else {                                 /* (FM-1: no combo here; the one-finger pair picks once, the owner
                                                    * 2026-10-16) */
                if (other)
                    cu.kpick_t0 = cu_now();
                cu_key_scale(k);
            }
        } else if (cu_fm1() && ((CU_TK_MASK >> k) & 1u))
            cu_tk_press(k);
        else if (cu_mod_of(k) != 0xFFu)
            cu_mod_press(k);
        return;
    }
    l = cu_layer();
    if (cu_rt_key(l, k)) {                         /* the Routing page's shortcuts (the owner, 2026-10-25: "allow the
                                                    * rest of the keys to work when on this page ... I can play notes
                                                    * and hear the effect"): C#4 D#4 F#4 G#4 A#4 alone are its */
        cu_layer_key(l, k);
        cu.key_note[k] = 0xFFu;
        return;
    }
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
        cu_trace("name: key %u: '%s' cursor %u of %u\n", (unsigned)k, cn.s, (unsigned)cn.cur, (unsigned)cn.len);
        cu.key_note[k] = 0xFFu;
        return;
    }
    /* (Options: the keys play as on the view it was opened from. Until 2026-10-12 the white roots jumped to the
     * first eleven settings and no key sounded: the owner, "it isn't really helpful, and also makes it difficult to
     * compare clean and classic". The menu is SELECT, KNOB 1 and the buttons only) */
    {
        int32_t n = 53 + (int32_t)k + 12 * cu.octave;
        n = n < 0 ? 0 : n > 127 ? 127 : n;
        if (cu.g_open) {                           /* FM-1: a type-key gesture not decided yet: the root waits for it
                                                    * (cu_tk_flush), nothing sounds until then (the owner, 2026-10-16) */
            cu.key_note[k] = (uint8_t)(n + 1);
            cu.kwait |= 1u << k;
            cu.kwait_up &= ~(1u << k);
            cu.kwait_vel[k] = (uint8_t)cu_key_vel(k);
            cu_trace("chord keys: key %u waits for the window\n", (unsigned)k);
            return;
        }
        cr_klat_post(CU_KEY_T0(k));                /* (78 PEAK: the key's way to the engine, timed) */
        cr_post(CRE_KEY, (uint32_t)n, cu_key_vel(k), 1);
        cu.last_vel = (uint8_t)cu_key_vel(k);
        cu_trace("touch: key %u vel %u%s\n", (unsigned)k, (unsigned)cu_key_vel(k), cs.touch ? cu_press_vel ? " sensed" : " context" : " fixed");
        cu.key_note[k] = (uint8_t)(n + 1);
    }
}

static void cu_key_release(uint32_t k)
{
    cu.kheld &= ~(1u << k);
    cr_sense_land[k] = 0;
    if (cu_rt.key == k + 1u)                       /* the Routing page's held block (whatever page shows now) */
        cu_rt_release();
    if (k >= 25u && cu.fsc_t0[k - 25u]) {          /* Full K Shorts' switch let go */
        cu_fsc_release(k - 25u);
        cu.key_note[k] = 0;
        return;
    }
    if (!cu_fm1() && k == CU_KEY_LOCK && !cu.key_note[k] && !((cu.kpick >> k) & 1u)) {
        if (cu.b3_pend) {                          /* let go before the hold time, nothing else pressed: the tap */
            cu.b3_pend = 0;
            cu_lock_tap();
        }
        return;
    }
    if (k < 9u && !cu.key_note[k]) {               /* (pressed as a chord key: not a Full-keyboard note, not a root) */
        if ((cu.kpick >> k) & 1u)                  /* pressed as the KEY layer's: nothing to end (the layer may be
                                                    * closed by now) */
            cu.kpick &= (uint16_t)~(1u << k);
        else if (cu_fm1() && ((CU_TK_MASK >> k) & 1u))
            cu_tk_release(k);
        else if (cu_mod_of(k) != 0xFFu)
            cu_mod_release(k);
        return;
    }
    if ((cu.kwait >> k) & 1u) {                    /* a root still waiting for the window: it plays then, briefly */
        cu.kwait_up |= 1u << k;
        return;
    }
    if (cu.key_note[k] && cu.key_note[k] != 0xFFu)
        cr_post(CRE_KEY, cu.key_note[k] - 1u, 0, 0);
    cu.key_note[k] = 0;
}

/* Options > Chord Keys (docs/CHORD-LAYOUT.md "Changing the layout"): every key held is released under the old map
 * (a held chord stops; LOCK's latch, a set of cr_mod_t, stays and shows on the new map's keys); those keys do nothing
 * more until they are let go (cu.kdead) */
static void cu_layout_set(uint32_t fm1)
{
    uint32_t k;
    fm1 = CU_FN_KEYS && fm1;
    if (cs.layout == fm1)
        return;
    cu_tk_quiet();
    for (k = 0; k < CU_NKEY; k++)
        if ((cu.kheld >> k) & 1u)
            cu_key_release(k);
    cu.kdead |= cu.kpanel;
    cu.kmidi = 0;
    cu.kheld = 0;
    cu.b3_pend = 0;
    cs.layout = (uint8_t)fm1;
    cu_trace("chord keys: layout %s\n", fm1 ? "FM-1" : "Classic");
}

/* Options > Full K Shorts (docs/CHORD-LAYOUT.md "The Full keyboard"): on the Full keyboard F#5 (i 0) shows the
 * layout's chord keys on the low keys (the Chords keyboard, its roots to F5) and G5 (i 1) plays a chord on every key;
 * a quick press toggles, a press held past Hold Time switches only while held. Neither plays a note */
static void cu_fsc_apply(uint32_t i, uint32_t on)
{
    uint32_t k;
    if (i == 0 && cu.full_chk != on) {
        if (!on)                                   /* the chord keys go: those held are let go (a held chord stops) */
            for (k = 0; k < cu_cb_end(); k++)
                if (((cu.kheld >> k) & 1u) && !cu.key_note[k]) {
                    cu_key_release(k);
                    cu.kdead |= (1u << k) & cu.kpanel;
                    cu.kmidi &= ~(1u << k);
                }
        cu_tk_quiet();
        cu.full_chk = (uint8_t)on;
    } else if (i == 1)
        cu.full_all = (uint8_t)on;
    cu_post_kbd();
    cu_message(i ? (on ? "chords" : "single notes") : (on ? "chord keys" : "notes"), CR_COL_WHITE);
    cu_trace("full: %s %s\n", i ? "G5 chords" : "F#5 chord keys", on ? "on" : "off");
}
static void cu_fsc_press(uint32_t i)
{
    uint32_t on = i ? cu.full_all : cu.full_chk;
    cu.fsc_was[i] = (uint8_t)on;
    cu.fsc_t0[i] = cu_now() | 1u;
    cu_fsc_apply(i, !on);
}
static void cu_fsc_release(uint32_t i)
{
    uint32_t held = cu_now() - cu.fsc_t0[i];
    cu.fsc_t0[i] = 0;
    if (held >= (uint32_t)HOLD_MS[settings_hold % 4u])   /* held: only while held */
        cu_fsc_apply(i, cu.fsc_was[i]);
}
static void cu_fsc_set(uint32_t on)               /* the Option */
{
    if (!on && cu_fshort() && cu.full_chk)
        cu_fsc_apply(0, 0);
    cs.full_short = (uint8_t)(on != 0);
    cu_fsc_enter();
    cu_post_kbd();
}

/* MIDI Keys (Options > MIDI Keys; docs/CHORD-LAYOUT.md "MIDI in"): cr_out.c's queue of the keys' channel, ISR -> UI:
 * a note n 53 .. 79 is the key n - 53, pressed as the keybed's (its velocity the sound's) through the active layout;
 * a key held on the keybed and over MIDI is one press, let go at the last. CR_MKQ_ALLUP: every MIDI key up */
static void cu_mkeys_poll(void)
{
    uint32_t e;
    while (cr_mkq_take(&e)) {
        uint32_t k = e & 31u, vel = (e >> 5) & 127u, down = (e >> 12) & 1u, bit;
        cu.last_input = cu_now();
        if (k == CR_MKQ_ALLUP) {
            for (k = 0; k < CU_NKEY; k++)
                if ((cu.kmidi >> k) & 1u) {
                    cu.kmidi &= ~(1u << k);
                    if (!((cu.kpanel >> k) & 1u))
                        cu_key_release(k);
                }
            continue;
        }
        if (k >= CU_NKEY)
            continue;
        bit = 1u << k;
        if (down) {
            if (cu.kmidi & bit)
                continue;
            cu.kmidi |= bit;
            if (!(cu.kpanel & bit)) {
                cu_press_vel = (uint8_t)(vel ? vel : 1u);
                cu_key_press(k);
                cu_press_vel = 0;
            }
        } else if (cu.kmidi & bit) {
            cu.kmidi &= ~bit;
            if (!(cu.kpanel & bit))
                cu_key_release(k);
        }
    }
}

static void cu_latch_toggle(void)                 /* LATCH: LFO tap (Classic), GLO held + LFO (FM-1) */
{
    uint32_t mode = CU_PERF[cs.perf_sel].mode;
    if (!cs.sticky && cs.perform_on && cs.par[mode][CR_P_HOLD]) {   /* the mode's Hold (Strum's KNOB 4, the arp's)
                                                   * lit the button: a tap turns Hold off and the held notes go, as a
                                                   * tap on a lit LATCH does (the owner, 2026-10-15: "you can't control
                                                   * this using the lock/hold button like you can elsewhere"); the next
                                                   * tap latches as before */
        cs.par[mode][CR_P_HOLD] = 0;
        cr_post(CRE_PARAM, mode, CR_P_HOLD, 0);   /* (cr_engine.c: Hold off releases the latched voices) */
        cu_trace("latch: %s hold off\n", CU_PERF[cs.perf_sel].short_name);
        return;
    }
    cs.sticky ^= 1u;
    cr_post(CRE_STICKY, 0, 0, cs.sticky);
}

static void cu_btn_press(uint32_t b)
{
    uint32_t octs = CU_BIT(B_OCTDN) | CU_BIT(B_OCTUP);
    cu.bheld |= CU_BIT(b);
    cu.last_input = cu.last_local = cu_now();
    if (b != BT_HOME)
        cu.home_hold = 0;
    if (b == B_OCTDN || b == B_OCTUP) {
        if (b == B_OCTDN)
            cu.oct_mod = 0;
        if ((cu.bheld & octs) == octs && !cu.oct_chord) {
            cu.oct_chord = 1;
            cu_panic();
        }
        if ((cu.bheld & octs) == octs && (cu.bheld & CU_BIT(BT_HOME)))
            cu.swallow |= CU_BIT(BT_HOME);        /* (HOME was down first: the same) */
        return;
    }
    if (b == BT_HOME) {                           /* (its release: home; a held button's release: no tap) */
        if ((cu.bheld & octs) == octs)            /* OCT- + OCT+ + HOME: the update gesture (cr_backup.c
                                                   * cr_update_poll): no view menu, and its release is no HOME tap */
            cu.swallow |= CU_BIT(b);
        else if (cu.armed != NB)
            cu.combo = 1;
        else {                                    /* alone: held CU_HOME_HOLD_MS opens the view menu (cu_scan) */
            cu.home_t0 = cu_now();
            cu.home_hold = 1;
        }
        return;
    }
    if (cu.armed == NB) {
        if (b == BT_EDIT && cu.page != PG_EDIT)   /* EDIT held: the engine picker of the chord sound */
            ce.part = 0;
        cu.armed = (uint8_t)b;
        cu.t0 = cu_now();
        cu.open = cu.combo = 0;
        cu.hclosed = 0;
        cu.blvl = 0;
        return;
    }
    /* another button during a hold: a combo (its release does nothing) */
    cu.swallow |= CU_BIT(b);
    if ((cu.armed == BT_OPT && b == BT_SAVE) || (cu.armed == BT_SAVE && b == BT_OPT))
        cu.rev_t0 = cu_now() | 1u;                /* GLO + SAVE: both held CU_REVERT_MS, the revert (cr_ui_input) */
#if CR_EDIT_HOOKS
    if (cu.page == PG_EDIT && cu.armed == BT_OPT && b == BT_EDIT) {   /* SHIFT + EDIT in the editor: chord / bass */
        cu.combo = 1;
        ce_button(b, CE_SHIFT);
        return;
    }
#endif
    cu_activity();
    if (cu.armed == BT_OPT && b == BT_METRO && !(cu.page == PG_EDIT && !cu_layer()))
        cu_metro_toggle();                         /* GLO held + SEQ: the click on / off, from anywhere but the sound
                                                    * editor (SHIFT and MOD there) */
    if (cu.armed == BT_OPT && b == BT_LATCH && cu_fm1() && !(cu.page == PG_EDIT && !cu_layer()))
        cu_latch_toggle();                         /* FM-1: GLO held + LFO, LATCH (LFO is LOCK there; LFO held, then
                                                    * GLO: a combo, the LOCK layer). Not in the sound editor (SHIFT and
                                                    * its LFO group there) */
    if (cu.armed == BT_OPT && b == BT_REC && !(cu.page == PG_EDIT && !cu_layer()))
        cu_rec_mode_next();                        /* GLO held + REC: the next record mode, in the same places (the
                                                    * loop editor too: no step is cleared). REC is swallowed above:
                                                    * no take, and REC held is undo only when it is pressed first */
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
        cu.home_hold = 0;
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
 * it and lands on the place it last had in that engine's pool (cs.pool_pos; the first preset before that). Where:
 * cu_epk_ok (not the chord part in the editor: its EDIT-held picker; nor the save dialog, nor the PLAY menu).
 * OPT held + ALGORITHM (the owner, 2026-10-12: "Bass presets, let's make that work similar to the main presets, GLO +
 * Bass knob to change engine"): the same for the BASS part (cu.epk_on 2, orange): the same engines, stepping and
 * landing rule (cs.pool_pos[1]). A bass that is off stays off (its sound waits on the new engine: a BASS tap or
 * ALGORITHM brings it). The other knob turned in the same hold: the first part's pick is committed, then the other's
 * picker opens */
static void cu_epk_step(uint32_t part, int32_t s)
{
    uint32_t n;
    int32_t r;
    cu_engine_at(0, &n);
    if (!n)
        return;
    if (cu.epk_on && cu.epk_on != part + 1u)
        cu_epk_commit();
    if (!cu.epk_on) {
        r = cu_engine_rank(trk[part ? CR_PART_BASS : CR_PART_CHORD].eng_req % NENGINES);
        cu.epk_on = (uint8_t)(part + 1u);
        cu.epk_rank = (uint8_t)(r < 0 ? 0 : r);
        cu.pop.until = 0;
    }
    cu.epk_rank = (uint8_t)(((int32_t)cu.epk_rank + s % (int32_t)n + (int32_t)n) % (int32_t)n);
    cu_trace("engine pick: %s%s (%u presets)\n", part ? "bass " : "", ENGINES[cu_engine_at(cu.epk_rank, 0)]->name,
             (unsigned)pool_count(cu_engine_at(cu.epk_rank, 0)) - 1u);
}
static void cu_epk_commit(void)                   /* OPT released: the engine picked */
{
    uint32_t e, pos, n, part;
    if (!cu.epk_on)
        return;
    part = cu.epk_on - 1u;
    cu.epk_on = 0;
    e = cu_engine_at(cu.epk_rank, 0);
    if (e == trk[part ? CR_PART_BASS : CR_PART_CHORD].eng_req % NENGINES) {
        cu_trace("sound: part %u engine %s kept\n", (unsigned)part, ENGINES[e]->name);
        return;
    }
    cu_pos_sync();                                 /* (the place in the engine left: remembered) */
    n = pool_count(e);
    pos = cs.pool_pos[part][eng_rank(e)];
    if (pos >= n)
        pos = n > 1u ? 1u : 0u;
    if (cuf_n(part, e))                            /* the favourites filter: the engine's first favourite of the part,
                                                    * not the place remembered (none: the place remembered) */
        pos = (uint32_t)cuf_move(part, 0, +1, 0, 0);
    cu_pool_load(part, e, pos);
    if (!part)
        cu_sends_to_fx();
    if (part)                                      /* (a bass that is off: still where its sound landed, not "00 off") */
        cu_pool_popup(1u, cu_part_pos(1));
    else
        cu_sound_popup(0);
    cu_trace("sound: part %u engine %s pos %u %s\n", (unsigned)part, ENGINES[e]->name, (unsigned)cu_part_pos(part),
             psnd[part].name);
}
static void cu_epk_screen(cr_screen_t *s);

/* BASS (ENV / BASS) held + ALGORITHM: the bass level (the owner, 2026-10-12: "hold the ENV/Bass key and turn bass knob
 * for the bass level shortcut"; OPT + ALGORITHM until then). Returns 1 when the turn was taken.
 *   no layer open, BASS not yet past the hold time: the level's popup, as OPT + ALGORITHM showed it, and NO layer (a
 *     shortcut that left the BASS layer locked open would need HOME after every use): the hold is spent (cu.blvl: no
 *     layer at the hold time, no tap on release); a key, another knob or a button touched in the same hold opens the
 *     layer as always (cu_activity)
 *   the BASS layer open (BASS held past the hold time, or held again in its locked layer, or a layer BASS's hold
 *     replaces): the level IN the layer, its LEVEL cell (KNOB 4's) hot, never the bass sound (ALGORITHM's own, which
 *     this combination turned until now)
 * Not in the loop editor (ALGORITHM is the step's voicing whatever is held) nor in the sound editor without a layer
 * (ENV is the editor's own button there) */
static int cu_bass_level_hold(int32_t s)
{
    int32_t v;
    if (cu.armed != BT_BASS || !((cu.bheld >> BT_BASS) & 1u) || (cu.ledit && cu.lock == L_LOOP))
        return 0;
#if CR_EDIT_HOOKS
    if (cu.page == PG_EDIT && !cu_layer() && ce_owns(BT_BASS))
        return 0;
#endif
    if (!cu.lock && !cu.hclosed && (!cu.open || cu.blvl)) {   /* the shortcut: no layer */
        cu.last_input = cu.last_local = cu_now();
        cu.home_hold = 0;
        cu.combo = cu.open = cu.blvl = 1;
    } else {                                       /* the layer's: opened by the hold as by any knob */
        cu_activity();
        if (cu.hclosed && !cu.lock) {              /* (BASS held again closed it and is still down: it opens again) */
            cu.hclosed = 0;
            cu_opened(BT_BASS);
        }
    }
    (void)v;
    trk[CR_PART_BASS].p[P_LEVEL] = (int16_t)cu_kval(trk[CR_PART_BASS].p[P_LEVEL], s, 0, 127, 0);
    if (cu_layer() == L_BASS)
        cu_hot_set(L_BASS, 3);
    else
        cu_popup_num(trk[CR_PART_BASS].p[P_LEVEL], 0, "", "bass level", CR_COL_ORANGE, 0, 127, 12);
    cu_trace("bass: level %d (BASS + ALGORITHM%s)\n", (int)trk[CR_PART_BASS].p[P_LEVEL],
             cu_layer() == L_BASS ? ", layer" : "");
    return 1;
}

/* the knobs on the naming page of Save as new (the owner, 2026-10-13: "the editor to name the preset needs work. All
 * the other knobs do default things which is weird. Select knob should select character, knob 1 to change the letter
 * at selected index, knob 4 CCW as a backspace, the other knobs 2 and 3 in the name editor should not do anything"):
 * SELECT the cursor (a step a turn; right from the last character: a new one), KNOB 1 the character under it, KNOB 4
 * left the backspace (the character under it; "The cursor should always sit at the character you are editing, not
 * the next character"); KNOB 2, KNOB 3, KNOB 4
 * right, PRESETS and ALGORITHM nothing, GLO or BASS held or not. Until then, with a name on the screen: KNOB 1 was the
 * voicing (the Full keyboard: the octave), KNOB 2 the name's last letter, KNOB 3 the Perform mode's knob, KNOB 4 the
 * chord part's send, SELECT the tempo, PRESETS the chord part's sound and ALGORITHM the bass part's (the sound being
 * named changed under its name), GLO + KNOB 1 the split point, GLO + SELECT the click level, BASS held + ALGORITHM
 * the bass level. A layer held open over the page (PERF, FX, ..) still owns the knobs while it is open; MASTER is not
 * a knob of this code */
static void cu_name_knob(uint32_t role, int32_t s)
{
    const char *what = "nothing";
    if (role == EN_SELECT) {
        cn_move(s > 0 ? 1 : -1);
        what = "cursor";
    } else if (role == EN_K1) {
        cn_knob(s);
        what = "letter";
    } else if (role == EN_K4 && s < 0) {
        cn_delete();
        what = "backspace";
    }
    cu_trace("name: %s (knob role %u %+d): '%s' cursor %u of %u\n", what, (unsigned)role, (int)s, cn.s, (unsigned)cn.cur,
             (unsigned)cn.len);
}
static void cu_knob(uint32_t role, int32_t s)
{
    uint32_t l;
    int32_t v;
    kn_turn(role, s, cu_now(), (uint32_t)cu_shift());   /* (the knob's speed, OPT held: cr_pages.c kn_*) */
    if (cu.page == PG_SAVE && ce.save_step && !cu_layer()) {   /* naming a sound: the knobs are the name's and nothing
                                                    * else (cu_name_knob): before the holds and shifts below */
        cu_activity();
        cu_name_knob(role, s);
        return;
    }
    if (role == EN_ALGO && cu_bass_level_hold(s))  /* BASS held + ALGORITHM: the bass level */
        return;
    cu_activity();
    if (cu.armed != NB && cu.hclosed) {            /* the button held again CLOSED its layer and is still down (the
                                                    * owner: "sometimes in the play menu turning the preset knob ends
                                                    * up changing the preset": LOOP held + PRESETS with the menu
                                                    * already open, turned after the hold time, reached the sound's
                                                    * pool). The knob is that layer's still: it opens again. The loop
                                                    * editor closed by EDIT held: nothing (not the slot under it) */
        if (cu.hclosed == 0xFFu)
            return;
        if (!cu.lock) {
            cu.hclosed = 0;
            cu_opened(cu.armed);
        }
    }
    if (cu.ledit && cu_layer() == L_LOOP && cu_shift()) {   /* the loop editor, GLO held: PRESETS jumps 8 steps, Steps
                                                    * (page 1, KNOB 2) snaps to 8 16 32 48 64, the voicing (ALGORITHM;
                                                    * page 2, KNOB 3) to Follow / 0 */
        if (role == EN_PRESET) {
            cu_ledit_move(s * 8);
            return;
        }
        if (role == EN_K2 && !cu.le_page) {
            cu_ledit_snap(s);
            return;
        }
        if (role == EN_ALGO || (role == EN_K3 && cu.le_page)) {   /* the step's voicing: Follow / 0 (GLO + ALGORITHM is
                                                    * the bass engine on the home pages: not in the editor) */
            cu_ledit_voic(s, role == EN_ALGO, 1);
            return;
        }
    }
    if (cu_shift() && cu.page == PG_EDIT && (role >= EN_K1 || role == EN_SELECT) && !cu_layer()) {
        if (role == EN_SELECT)                     /* the editor wins: SHIFT + SELECT steps as SELECT */
            ce_select(s);
        else                                       /* .. SHIFT + a knob: the value snaps (cr_pages.c kn_snap) */
            ce_knob(role - EN_K1, s, 1);
        return;
    }
    if (cu_shift() && (role == EN_PRESET || role == EN_ALGO) && cu_epk_ok(role == EN_ALGO)) {
        cu_epk_step(role == EN_ALGO, s);           /* OPT held + PRESETS: the chord part's engine (on OPT's release);
                                                    * + ALGORITHM: the bass part's, the same way; in a layer and for
                                                    * the bass in the sound editor too since 2026-10-12 (cu_epk_ok:
                                                    * wherever the knob's plain job is the part's sound). Where it is
                                                    * not (the PLAY menu's slot and record mode, the editor's picker,
                                                    * the save dialog) OPT changes nothing about the knob. The bass
                                                    * level: BASS held + ALGORITHM */
        return;
    }
    if (cu_shift() && (role == EN_K1 || role == EN_SELECT)) {   /* OPT held: second functions (the other knobs: their
                                                                 * own) */
        if (role == EN_K1) {                /* the Single Notes split point */
            cs.split = (uint8_t)((cs.split + 12 + s % 12) % 12);
            cu_post_single();
            cu_popup(CU_NOTE[cs.split], cu_full() ? "Full: at F4" : cs.single == 2u ? "split bass" :
                     cs.single ? "split oct" : "split off", "split point", CR_COL_BLUE, cs.split, 0, 11, 12);
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
        if (cu.ledit)                              /* (the loop editor: the step cursor, the step's voicing: never
                                                    * the slot, never the sound) */
            cu_ledit_move(s);
        else
            cu_loop_turn(s);
        return;
    }
    if (l == L_LOOP && role == EN_ALGO) {
        if (cu.ledit)
            cu_ledit_voic(s, 1, 0);
        else
            cu_loop_mode_turn(s);
        return;
    }
    if (l == L_LOOP && role == EN_SELECT) {        /* the PLAY menu: SELECT turns its page (the owner, 2026-10-11: until
                                                    * then the length, now KNOB 1); the loop editor: its page */
        uint32_t pg = s > 0 ? 1u : 0u;
        if (cu.ledit) {
            if (pg != cu.le_page) {
                cu.le_page = (uint8_t)pg;
                cu_hot.knob = 0;
                cu_trace("loopedit: page %u\n", (unsigned)pg + 1u);
            }
            return;
        }
        if (pg != cu.lpage) {
            cu.lpage = (uint8_t)pg;
            cu_hot.knob = 0;
            cu_trace("loop: page %u\n", (unsigned)pg + 1u);
        }
        return;
    }
    if (l == L_KEY && role == EN_SELECT) {         /* the KEY layer: SELECT turns its page (no tempo there) */
        uint32_t pg = s > 0 ? 1u : 0u;
        if (pg != cu.kpage) {
            cu.kpage = (uint8_t)pg;
            cu_hot.knob = 0;
            cu_trace("key: page %u\n", (unsigned)pg + 1u);
        }
        return;
    }
    if (l && role == EN_SELECT && l != L_LOCK) {   /* a picker: SELECT moves (LOCK: the tempo; KEY: its page, above) */
        v = cu_layer_sel(l) + s;
        v = v < 0 ? 0 : v >= cu_layer_count(l) ? cu_layer_count(l) - 1 : v;
        cu_layer_pick(l, v);
        return;
    }
    if (cu.opt_open && (role == EN_SELECT || role == EN_K1)) {
        if (role == EN_SELECT) {
            cu.opt_sel = (uint8_t)cu_opt_next(cu.opt_sel, s);   /* (the entries listed: cu_opt_vis) */
        } else {
            opt_set(cu.opt_sel, cu.opt_sel == O_VEL ? cu_kval(opt_get(O_VEL), s, 1, 127, kn.snap) : opt_get(cu.opt_sel) + s);
                                                   /* (Velocity: a value; the others are choices, one a detent) */
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
    if (cu.page == PG_SAVE && !ce.save_step && role == EN_SELECT) {   /* the SAVE menu, in the Options' grammar (the
                                                    * owner, 2026-10-13: "make knob 1 work for changing values to match
                                                    * menu behaviour in other menus like GBL"): SELECT picks the entry, a
                                                    * turn a step, stopping at the ends: Overwrite, Save as new, Favorite,
                                                    * Favorites filter, Favorite type; P5's menu: the last three */
        int32_t lo = cu_save_favonly(ce.save_part) ? (int32_t)CU_SAVE_FAV : 0;
        v = (int32_t)ce.save_choice + (s > 0 ? 1 : -1);
        ce.save_choice = (uint8_t)(v < lo ? lo : v > (int32_t)CU_SAVE_N - 1 ? (int32_t)CU_SAVE_N - 1 : v);
        ce.del_ask = 0;
        cu_trace("save: choice %s\n", CU_SAVE_NAME[ce.save_choice]);
        return;
    }
    if (cu.page == PG_SAVE && !ce.save_step && (role == EN_K1 || role == EN_K2 || role == EN_K3 || role == EN_K4)) {
        /* KNOB 1 changes the entry's value (Favorite: Off / On; the filter: Off / On; the type: Both / Chord / Bass);
         * Overwrite and Save as new have none: nothing. KNOB 4 is the sound's mark on every entry ("Knob 4 should set
         * favorite regardless of which setting is selected"): right marks, left takes it off, at once. (Until this
         * menu KNOB 1 also moved the choice, and KNOB 4 on the two saves was the chord part's send as on the home
         * pages; on the naming page both are still what they are there.) KNOB 3 is the sound's TYPE on every entry
         * (the owner, 2026-10-13: "Fix knob 3 in the preset save menu to set the type too"): what KNOB 1 does on the
         * Favorite type entry, and on another entry a short message says the type it gave (until then KNOB 3 here was
         * the Perform mode's knob, as on the home pages; on the naming page it still is). KNOB 2 is the favourites
         * filter on every entry, left Off, right On: what KNOB 1 does on the Favorites filter entry, with a message of
         * the state on another entry (until then KNOB 2 here was the bass register, as on the home pages; on the
         * naming page it is the last letter, unchanged). So: SELECT the entry, KNOB 1 its value, KNOB 2 the filter,
         * KNOB 3 the type, KNOB 4 the mark. Over the red question: none of them */
        if (ce.del_ask)
            return;
        if (role == EN_K2) {
            cu_filter_turn(s);
            if (ce.save_choice != CU_SAVE_FILT)
                cu_message(crf_filter ? "Filter on" : "Filter off", CR_COL_YELLOW);
        } else if (role == EN_K3) {
            if (cu_fav_type_turn(ce.save_part, s) && ce.save_choice != CU_SAVE_TYPE) {
                uint32_t tag = 0, id = 0;
                char m[24];
                (void)cu_fav_part_key(ce.save_part, &tag, &id);
                cu_cpy(m, "Type: ", sizeof m);
                cu_cat(m, CU_FAV_TYPE[crf_type(tag, id)], sizeof m);
                cu_message(m, CR_COL_YELLOW);
            }
        } else if (role == EN_K4 || ce.save_choice == CU_SAVE_FAV)
            (void)cu_fav_turn(ce.save_part, s);
        else if (ce.save_choice == CU_SAVE_FILT)
            cu_filter_turn(s);
        else if (ce.save_choice == CU_SAVE_TYPE)
            (void)cu_fav_type_turn(ce.save_part, s);
        else
            cu_trace("save: knob 1 on %s: no value\n", CU_SAVE_NAME[ce.save_choice]);
        return;
    }
    switch (role) {
    case EN_K1:                                    /* the voicing, on the Full keyboard too since 0.24 (chords play
                                                    * there: Key plays Chords, G5; docs/CHORD-LAYOUT.md); the octave is
                                                    * OCT- / OCT+'s */
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
        cu_send_set(0, CU_FX[cs.fx_sel].send, cu_send_step(cu_send(0, CU_FX[cs.fx_sel].send), s, kn.snap));
        cu_send_popup(cu_send(0, CU_FX[cs.fx_sel].send), CU_FX[cs.fx_sel].name, CR_COL_GREEN);
        cu_trace("send: knob 4 %s %u (%u%%)\n", CU_FX[cs.fx_sel].name, (unsigned)cu_send(0, CU_FX[cs.fx_sel].send),
                 (unsigned)cu_send_pct(cu_send(0, CU_FX[cs.fx_sel].send)));
        break;
    case EN_PRESET:                                /* the chord part's engine's pool, wrapping */
        {
            uint32_t n = pool_count(trk[CR_PART_CHORD].eng_req % NENGINES);
            if (cuf_n(0, trk[CR_PART_CHORD].eng_req))   /* the favourites filter: the chord part's favourites, wrapping */
                cu_sound_go((uint32_t)cuf_move(0, (int32_t)cs.sound, s, 1, 0));
            else
                cu_sound_go((cs.sound + (uint32_t)(s % (int32_t)n + (int32_t)n)) % n);
            cu_sound_popup(0);
            cu_trace("sound: part 0 pos %u %s\n", (unsigned)cs.sound, psnd[0].name);
        }
        break;
    case EN_ALGO:                                  /* OFF, then the bass part's engine's pool, wrapping */
        {
            int32_t n = (int32_t)pool_count(trk[CR_PART_BASS].eng_req % NENGINES) + 1;
            v = ((int32_t)cs.bass_sound + s % n + n) % n;
            if (cuf_n(1, trk[CR_PART_BASS].eng_req))   /* the favourites filter: OFF, then the bass part's favourites */
                v = cuf_move(1, (int32_t)cs.bass_sound - 1, s, 1, 1) + 1;
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
    for (e = CU_ENC0; e < CU_NENC; e++)
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
        pe &= ~cc.used & CU_BTN_MASK;
        if (!pe)
            return;
        for (id = 0; id < CU_NBTN; id++)
            if ((pe >> id) & 1u)
                break;
        panel.btn[cc.step] = (uint8_t)id;
        cc.used |= 1u << id;
        cu_trace("calib: %s = button %u\n", B_NAME[cc.step], (unsigned)id);
        if (++cc.step == NB) {
            cc.used = 0;
            for (e = CU_ENC0; e < CU_NENC; e++)
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
            for (e = CU_ENC0; e < CU_NENC; e++)
                if ((cc.used >> e) & 1u)
                    fm1_enc_take(e);
            if (cc.step == CU_CAL_DONE)
                return;
        }
        for (e = CU_ENC0; e < CU_NENC; e++)
            if (!((cc.used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                break;
        if (e == CU_NENC)
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
    for (e = CU_ENC0; e < CU_NENC; e++)           /* the last step: the knobs do nothing */
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

/* a key down / up on the keybed (cu.kpanel) or by MIDI Keys (cu.kmidi): the press acts at the first, the release at
 * the last; a key held through a layout change (cu.kdead) does nothing until it is let go */
static void cu_kp_down(uint32_t k)
{
    uint32_t bit = 1u << k;
    cu.kpanel |= bit;
    if (!(cu.kmidi & bit))
        cu_key_press(k);
}
static void cu_kp_up(uint32_t k)
{
    uint32_t bit = 1u << k;
    cu.kpanel &= ~bit;
    if (cu.kdead & bit)
        cu.kdead &= ~bit;
    else if (!(cu.kmidi & bit))
        cu_key_release(k);
}
/* The note keys' part of the scan: the frame's (cr_ui_input) and the pump's (cr_ui_pump, between the strips of a
 * redraw). ne: the press edges taken, km: the keys down */
static void cu_scan_keys(uint32_t ne, uint32_t km)
{
    uint32_t k, now = cu_now();
    cu_mkeys_poll();                               /* (MIDI Keys: the keys MIDI pressed or let go) */
    cu_tk_poll();                                  /* (FM-1: the type keys' window closed) */
    for (k = 0; k < CU_NKEY; k++) {
        uint32_t was = (cu.kpanel >> k) & 1u, is = (km >> k) & 1u, e = (ne >> k) & 1u;
        if (was && (e || !is))
            cu_kp_up(k);
    }
    for (k = 0; k < CU_NKEY; k++) {                /* the keys waiting for their velocity: play when it is there */
        uint32_t bit = 1u << k, v;
        if (!(cu.kpend & bit))
            continue;
        v = cr_sense_take(k);
        if (!v && now - cu.kpend_t0[k] < CU_VEL_WAIT_MS)
            continue;
        cu.kpend &= ~bit;
        cr_sense_land[k] = 0;
        if (v)
            cu.ctx_vel = (uint8_t)(cu.ctx_vel ? (cu.ctx_vel * 3u + v + 2u) / 4u : v);
        cu_press_vel = (uint8_t)v;
        cu_kp_down(k);
        cu_press_vel = 0;
        if (!((km >> k) & 1u))
            cu_kp_up(k);
    }
    for (k = 0; k < CU_NKEY; k++) {
        uint32_t was = (cu.kpanel >> k) & 1u, is = (km >> k) & 1u, e = (ne >> k) & 1u;
        if (!was && (is || e) && !((cu.kpend >> k) & 1u)) {
            if (CU_KEY_SENSED(k) && !cr_sense_land[k]) {   /* (the velocity comes 7 .. 15 ms after the debounce) */
                cu.kpend |= 1u << k;
                cu.kpend_t0[k] = now;
                continue;
            }
            cu_press_vel = (uint8_t)cr_sense_take(k);
            cr_sense_land[k] = 0;
            if (cu_press_vel)
                cu.ctx_vel = (uint8_t)(cu.ctx_vel ? (cu.ctx_vel * 3u + cu_press_vel + 2u) / 4u : cu_press_vel);
            cu_kp_down(k);
            cu_press_vel = 0;
            if (!is)
                cu_kp_up(k);
        }
    }
}
static void cu_scan_step(void)                     /* step entry: every key up (nothing latched) after a chord: the
                                                    * cursor advances (the engine: if a chord was written there) */
{
    static uint8_t down;
#if BOARD_SMK37
    uint8_t dn = (uint8_t)(cu.kheld || cu_xheld || (cu.mlatch && !cu_full()));
#else
    uint8_t dn = (uint8_t)(cu.kheld || (cu.mlatch && !cu_full()));   /* (the Full keyboard: the latch waits) */
#endif
    if (cr_snap.lcap == CRL_CAP_STEP && down && !dn) {
        cr_post(CRE_LOOP, LP_STEP, 0, CRL_STEP_KEYSUP);
        cu_trace("loop: step keys up\n");
    }
    down = dn;
}

/* The keys while a frame draws (docs/INTEGRATION.md "Keys while a frame draws"). A redraw calls this at each glyph, arc
 * and polyline it draws and after each strip (cr_gfx.c cr_draw_idle, set by cr_ui_draw for its own cr_draw only): a
 * key that plays is handed to the engine then, not after the frame. Only that: the note keys' part of the scan, and only while it can do nothing but post
 * to the engine (cu_key_press: B3's tap, a chord key, a note; cu_key_release). So it stands back, and the frame's
 * own scan (cr_ui_input, in order, as before) takes everything, whenever
 *   a button is down, was pressed or let go since the scan, or a hold is armed (the keys may mean a layer's, a
 *     combo opens a layer: cu_activity, and a button's event comes before a key's);
 *   the root keys belong to the layer open (cu_layer_keys: they pick, load, save) or to the naming page (Options
 *     never takes a key: they play there);
 *   the panel is being calibrated.
 * Nothing it calls draws, reads the flash or writes what the frame being drawn reads: cr_draw reads its screen
 * model (cu_scr, built before it: cr_build_screen, cu_animate), its cache, the canvas, the glyph buffer and the
 * palette, and the key handlers write none of them (cu, cs and the engine's queue only), so the frame is drawn as
 * it was built and the next one shows the keys. Not re-entered: cr_draw is not called from here.
 * With no key gone down or up since the last scan it costs a comparison.
 * CU_PUMP_BEGIN / END: the emulator's (EMU_PUMP=0: no pump; the CPU lock, as its emu_fw_idle) */
#ifndef CU_PUMP_BEGIN
#define CU_PUMP_BEGIN() 1
#define CU_PUMP_END() ((void)0)
#endif
#ifndef CU_BTN_EDGES
#define CU_BTN_EDGES() (fm1_in.pressed | fm1_in.released)   /* button edges the scan has not taken yet */
#define CU_KEY_EDGES() fm1_in.notes_pressed                 /* .. note key presses */
#endif
static uint32_t cu_pumped;                         /* key scans done inside a redraw (the emulator's "keys:" line) */
static void cr_ui_pump(void)
{
    if (!CU_PUMP_BEGIN())
        return;
    if ((fm1_in.notes != cu.kheld || CU_KEY_EDGES()) &&
        !cc.on && !cc.req && !fm1_in.buttons && !CU_BTN_EDGES() && !cu.bheld && cu.armed == NB &&
        !cu_layer_keys(cu_layer()) && !(cu.page == PG_SAVE && ce.save_step)) {
        uint32_t ne = fm1_input_note_edges();
        cu_scan_keys(ne, fm1_in.notes);
        cu_scan_step();
        cu_touch_poll();
        cu_pumped++;
    }
    CU_PUMP_END();
}

#if BOARD_SMK37
/* ------------------------------------------------------------ SMK-37 --- */
/* The SMK-37's extras (SMK37.md): the outer keys, the pads of their own, KNOB BANK, FADER BANK, < and >, the faders
 * and the wheels. The 27 middle keys, the chord grid's pads, the 14 buttons of the FM-1 and the knobs go through
 * the FM-1's paths above. Nothing here runs while the panel is being calibrated */

/* an outer key (keybed 0..4 = C3..E3, 32..36 = G#5..C6) down (vel) or up (0): a root at its own pitch, as a root key
 * plays (the chord held or latched over it; the Full keyboard: a note), with the octave shift. Where the root keys
 * have another job (a layer's map, naming) the outer keys have none */
static void cu_smk_xkey(uint32_t kb, uint32_t vel)
{
    uint32_t x = kb < 5u ? kb : kb - 27u;
    if (x >= 10u)
        return;
    if (cu_xnote[x]) {                             /* (its last note, whatever ended its press) */
        cr_post(CRE_KEY, cu_xnote[x] - 1u, 0, 0);
        cu_xnote[x] = 0;
    }
    if (!vel) {
        cu_xheld &= (uint16_t)~(1u << x);
        return;
    }
    cu_xheld |= (uint16_t)(1u << x);
    cu.last_sound = cu_now();
    cu_activity();
    if (cu.b3_pend) {                              /* B3 still down and another key: LOCK's tap now (cu_key_press) */
        cu.b3_pend = 0;
        cu_lock_tap();
    }
    if (cu_layer_keys(cu_layer()) || (cu.page == PG_SAVE && ce.save_step))
        return;
    {
        int32_t n = 48 + (int32_t)kb + 12 * cu.octave;
        n = n < 0 ? 0 : n > 127 ? 127 : n;
        cr_klat_post(fm1_in.ks_t1[kb]);
        vel = cs.touch ? vel : cs.vel;             /* (Options > Key Touch, as cu_key_vel) */
        cr_post(CRE_KEY, (uint32_t)n, vel, 1);
        cu.last_vel = (uint8_t)vel;
        cu_xnote[x] = (uint8_t)(n + 1);
    }
}

/* a pad down (vel) or up (0): its chord block key, as cu_key_press / cu_key_release play it on the FM-1 (a type or an
 * extension while held, with LOCK's latch; MIN / MAJ the scale in the KEY layer with its keys on pick); LOCK: B3's tap
 * (its release) and hold (cu_smk_input) */
static void cu_smk_pad(uint32_t pad, uint32_t vel)
{
    int32_t k = pad < 16u ? CU_PAD_FN[pad] : -1, i = pad < 16u ? CU_PAD_PERF[pad] : -1;
    uint32_t bit;
    if (i >= 0 && vel) {                           /* a mode pad: that mode, performance on; the mode playing: off (as
                                                    * a PERF tap). The same state as PERF and the PERF layer's picker */
        cu_activity();
        if (cs.perform_on && cs.perf_sel == (uint32_t)i)
            cu_perf_toggle();
        else {
            cu_perf_pick((uint32_t)i);
            if (!cs.perform_on)
                cu_perf_toggle();
        }
        cu_message(cs.perform_on ? CU_PERF[i].name : "perform off", CR_COL_WHITE);
        return;
    }
    if (k < 0)
        return;
    bit = 1u << k;
    if (!vel) {
        if (!(cu_pheld & bit))
            return;
        cu_pheld &= (uint16_t)~bit;
        if ((uint32_t)k == CU_KEY_LOCK) {
            if (cu.b3_pend) {                      /* let go before the hold time, nothing else pressed: the tap */
                cu.b3_pend = 0;
                cu_lock_tap();
            }
        } else if (cu_ppick & bit)
            cu_ppick &= (uint16_t)~bit;
        else
            cu_mod_release((uint32_t)k);
        return;
    }
    if (cu_pheld & bit)
        return;
    cu_pheld |= (uint16_t)bit;
    cu_activity();
    if (cu.b3_pend && (uint32_t)k != CU_KEY_LOCK) {   /* LOCK's pad still down and another: LOCK's tap now */
        cu.b3_pend = 0;
        cu_lock_tap();
    }
    if ((uint32_t)k == CU_KEY_LOCK) {
        cu.b3_pend = 1;
        cu.b3_t0 = cu_now();
        return;
    }
    if (cu_key_pick()) {                           /* the KEY layer: MAJ / MIN pick the scale */
        cu_ppick |= (uint16_t)bit;
        cu_key_scale((uint32_t)k);
        return;
    }
    cu_mod_press((uint32_t)k);
}

/* < and >: SELECT's step where it picks or pages (a layer, a menu, the Options, the editor, the save dialog: never the
 * tempo); on the view, the view before / after (HOME tapped goes forward) */
static int cu_smk_paging(void) { return cu_layer() || cu.opt_open || cu.page != PG_NONE; }
static void cu_smk_step(int32_t s)
{
    if (cu_smk_paging())
        cu_knob(EN_SELECT, s);
    else if (!cs.view_lock)
        cs.view = (uint8_t)((cs.view + (s > 0 ? 1u : V_N - 1u)) % V_N);
}

/* the four extra buttons (smk_input.h ids 0..3), unless the calibration gave one of them an FM-1 label */
static uint32_t cu_smk_free(void)
{
    uint32_t b, used = 0;
    for (b = 0; b < NB; b++)
        used |= 1u << panel.btn[b];
    return ~used & 0xFu;
}
static void cu_smk_button(uint32_t id)
{
    cu_activity();
    switch (id) {
    case SMK_BTN_KNOB_BANK:                        /* the view menu (HOME held); again: closed */
        if (cu.lock == L_VIEW)
            cu_layer_close();
        else
            cu_view_menu();
        break;
    case SMK_BTN_FADER_BANK:                       /* the faders: the chord part's sends, the bass part's */
        cu_fbank ^= 1u;
        cu_fbank_new = 1;
        cu_message(cu_fbank ? "faders: bass sends" : "faders: chord sends", cu_fbank ? CR_COL_ORANGE : CR_COL_GREEN);
        break;
    default:
        cu_smk_step(id == SMK_BTN_RIGHT ? 1 : -1);
        break;
    }
}

static void cu_smk_input(uint32_t pe)
{
    uint32_t id, free = cu_smk_free();
    int32_t e;
    for (id = 0; id < 4u; id++)
        if (((pe & free) >> id) & 1u)
            cu_smk_button(id);
    while ((e = smk_extkey_take()) >= 0)
        cu_smk_xkey((uint32_t)e >> 8, (uint32_t)e & 0xFFu);
    while ((e = smk_pad_take()) >= 0)
        cu_smk_pad((uint32_t)e >> 8, (uint32_t)e & 0xFFu);
    {                                              /* K1 turned (hal/smk_input.h smk_master_seq; GitHub issue 30):
                                                    * the volume, in per cent of the pot's range, as the click level's */
        static uint8_t seen;
        uint8_t q = smk_master_seq;
        if (q != seen) {
            seen = q;
            cu_activity();
            cu_popup_num((smk_master * 100 + 511) / 1023, 0, "%", "volume", CR_COL_WHITE, 0, 100, 10);
        }
    }
    if (cu.b3_pend && ((cu_pheld >> CU_KEY_LOCK) & 1u) &&
        cu_now() - cu.b3_t0 >= (uint32_t)HOLD_MS[settings_hold % 4u]) {
        cu.b3_pend = 0;                            /* LOCK's pad held alone past the hold time: the LOCK layer */
        cu_lock_menu();
    }
}

/* the faders: FADER 1..4 = the sends into Drive, Chorus, Delay, Reverb (the Sends page's and the editor's FX group's
 * order), of the chord part (bank A) or the bass part (bank B), shown as an incoming CC shows them. A fader takes
 * over only once it moves (also after a bank change), so nothing jumps */
static void cu_smk_faders(void)
{
    static const uint8_t FX_OF[4] = {3, 1, 2, 0};   /* CU_FX: Reverb, Chorus, Delay, Drive */
    static uint8_t last[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint32_t i;
    if (cu_fbank_new) {
        cu_fbank_new = 0;
        for (i = 0; i < 4u; i++)
            last[i] = 0xFF;
    }
    for (i = 0; i < 4u; i++) {
        uint32_t v = fm1_in.fader[i], fx = FX_OF[i];
        if (last[i] == 0xFF || fm1_in.frames < 200u) {   /* (the first readings settle) */
            last[i] = (uint8_t)v;
            continue;
        }
        if (v == last[i])
            continue;
        last[i] = (uint8_t)v;
        if (cc.on)
            continue;
        cu_send_set(cu_fbank, CU_FX[fx].send, (int32_t)v);
        cu_send_popup((int32_t)v, CU_FX[fx].name, cu_fbank ? CR_COL_ORANGE : CR_COL_GREEN);
        cu.last_input = cu.last_local = cu_now();
    }
}

/* the wheels (mux C addresses 4 = pitch, 5 = mod), self-calibrating: the pitch wheel's rest is its centre (taken at
 * power-on, followed slowly while it rests), each wheel's range grows to what it has reached (the travel is short:
 * raw ~445..640 pitch, ~474..629 mod on a unit). Pitch: a 14-bit bend with a dead band around the centre; mod:
 * 0..127, up (lower raw) = more. The audio ISR plays them (cr_out.c cr_smk_wheel_block) */
static void cr_smk_wheels(void)
{
    enum { DEAD = 12, SPAN0 = 80, MOD_LO = 485, MOD_HI = 615 };
    static int32_t pc, plo, phi, mlo, mhi, rest;
    int32_t p = fm1_in.wheel[0], m = fm1_in.wheel[1], d, b, mw;
    if (fm1_in.frames < 300u)                     /* (the readings settle) */
        return;
    if (!pc) {
        pc = p;
        plo = p - SPAN0;
        phi = p + SPAN0;
        mlo = m < MOD_LO ? m : MOD_LO;
        mhi = m > MOD_HI ? m : MOD_HI;
    }
    if (p < plo)
        plo = p;
    if (p > phi)
        phi = p;
    if (m < mlo)
        mlo = m;
    if (m > mhi)
        mhi = m;
    d = p - pc;
    if (d > -DEAD && d < DEAD) {
        b = 0;
        if (++rest > 200) {                        /* resting: the centre follows a slow drift */
            pc += d > 0 ? 1 : d < 0 ? -1 : 0;
            rest = 0;
        }
    } else {
        rest = 0;
        b = d > 0 ? (d - DEAD) * 8191 / (phi - pc - DEAD > 1 ? phi - pc - DEAD : 1)
                  : (d + DEAD) * 8192 / (pc - plo - DEAD > 1 ? pc - plo - DEAD : 1);
        b = b < -8192 ? -8192 : b > 8191 ? 8191 : b;
    }
    mw = mhi - m <= DEAD ? 0 : (mhi - m - DEAD) * 127 / (mhi - mlo - DEAD > 1 ? mhi - mlo - DEAD : 1);
    mw = mw < 0 ? 0 : mw > 127 ? 127 : mw;
    if (b != cr_smk_bend || (uint32_t)mw != cr_smk_modw)
        cu.last_input = cu.last_local = cu_now();  /* (a wheel is a touch: the screensaver) */
    cr_smk_bend = (int16_t)b;
    cr_smk_modw = (uint8_t)mw;
}

/* the pedal jack (mux C address 6, hal/smk_input.h fm1_in.pedal): a pull-up, ~985 with nothing plugged in, no plug
 * detect. What a pedal reads is not known here (no unit to try; the stock firmware takes 1023 - raw through its
 * calibration record's fifth curve: raw ~0..170 the active range), so both modes learn. Options > Pedal:
 *   Sustain     a switch pedal of either polarity: its rest is learnt once the readings settle at power-on and again
 *               at every change of the option; down = more than 400 counts from it (up again within 250), steady for
 *               8 scan frames (~13 ms). A normally-closed pedal plugged in after power-on reads as held until then
 *   Expression  0..127, lower raw = more (as the stock firmware's inversion). The range is what the pedal has
 *               reached (a dead band inside each end: both reachable), learnt once it has rested plugged in ~0.3 s;
 *               nothing is sent until that range spans 48 counts (an empty jack sets nothing: the parts stay at full
 *               expression, 127). The empty jack's reading (940 and up) is no pedal: back to 127, the range learnt
 *               again from the next pedal. A step of 2 or an end, as the faders (no jitter)
 * Leaving a mode lets go of what it held: a sustain up, the expression back to 127. Once per scan frame (~1.6 ms),
 * called with the wheels; a change is a touch (the screensaver). The audio ISR plays them (cr_out.c
 * cr_smk_pedal_block) */
static void cr_smk_pedal(void)
{
    enum { OPEN = 940, DOWN = 400, UP = 250, DEB = 8, PLUG = 200, SPAN = 48, DEAD = 6 };
    static uint32_t seen, t_flip, t_open;
    static int32_t acc, rest, lo, hi;
    static uint8_t ready, mode, on, moved;
    uint32_t fr = fm1_in.frames;
    int32_t r, d, e, x;
    if (fr < 300u || fr == seen)                   /* (the readings settle) */
        return;
    seen = fr;
    if (!ready)
        acc = (int32_t)fm1_in.pedal * 16;
    acc += ((int32_t)fm1_in.pedal * 16 - acc) / 4; /* x16 IIR */
    r = acc / 16;
    if (!ready || mode != cs.smk_pedal) {          /* power-on, or the option changed: start again */
        if (on || cr_smk_expr != 127u)
            cu.last_input = cu.last_local = cu_now();
        cr_smk_sus = 0;
        cr_smk_expr = 127;
        ready = 1;
        mode = cs.smk_pedal;
        on = moved = 0;
        rest = r;
        t_flip = t_open = fr;
        lo = 1023;
        hi = 0;
        return;
    }
    if (!mode) {                                   /* Sustain */
        d = r > rest ? r - rest : rest - r;
        if ((on ? d > UP : d > DOWN) == on)
            t_flip = fr;
        else if (fr - t_flip >= DEB) {
            on ^= 1u;
            cr_smk_sus = on;
            cu.last_input = cu.last_local = cu_now();
        }
        return;
    }
    if (r >= OPEN) {                               /* Expression: the jack empty */
        t_open = fr;
        lo = 1023;
        hi = 0;
        if (moved) {
            moved = 0;
            cr_smk_expr = 127;
            cu.last_input = cu.last_local = cu_now();
        }
        return;
    }
    if (fr - t_open < PLUG)                        /* (a plug going in: its contacts settle) */
        return;
    lo = r < lo ? r : lo;
    hi = r > hi ? r : hi;
    if (!moved && hi - lo < SPAN)
        return;
    moved = 1;
    e = hi - r <= DEAD ? 0 : r - lo <= DEAD ? 127 : (hi - DEAD - r) * 127 / (hi - lo - 2 * DEAD);
    e = e < 0 ? 0 : e > 127 ? 127 : e;
    x = cr_smk_expr;
    if (e > x + 1 || e + 1 < x || (e != x && (e == 0 || e == 127))) {
        cr_smk_expr = (uint8_t)e;
        cu.last_input = cu.last_local = cu_now();
    }
}

static void cr_smk_frame(void)                     /* main.c, once a frame */
{
    cu_smk_faders();
    cr_smk_wheels();
    cr_smk_pedal();
}
#endif

static void fm6_service(void);                     /* fm6_store.c (after this file): DX7 SysEx */
#if FELUCCA_CZ
static void cz_service(void);                      /* cz_store.c (after this file): Casio CZ-1 SysEx */
#endif
static void cr_ui_input(void)
{
    uint32_t pe = fm1_input_edges(0), ne = fm1_input_note_edges(), bm = fm1_in.buttons, km = fm1_in.notes;
    uint32_t cur = 0, edg = 0, b, now = cu_now();
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
        now - cu.t0 >= 1000u) {                    /* naming: SAVE held 1 s asks to delete the slot */
        cu.combo = 1;                              /* (its release: no cancel) */
        cu_save_ask();
    }
    if (cu.home_hold && !cc.on && ((cu.bheld >> BT_HOME) & 1u) && now - cu.home_t0 >= CU_HOME_HOLD_MS) {
        cu.home_hold = 0;                          /* HOME held: the view menu; its release does nothing */
        cu.swallow |= CU_BIT(BT_HOME);
        if (cu.lock == L_VIEW)                     /* .. held again in its menu: closed (as every layer's button) */
            cu_layer_close();
        else
            cu_view_menu();
    }
    if (cu.rev_t0) {                               /* GLO + SAVE, both still down: the revert after CU_REVERT_MS */
        if ((cu.bheld & (CU_BIT(BT_OPT) | CU_BIT(BT_SAVE))) != (CU_BIT(BT_OPT) | CU_BIT(BT_SAVE)))
            cu.rev_t0 = 0;
        else if (now - cu.rev_t0 >= CU_REVERT_MS) {
            cu.rev_t0 = 0;
            cu_revert();
        }
    }
    if (cu.armed != NB && !cu.open && ((cu.bheld >> cu.armed) & 1u) && !(cu.armed == BT_SAVE && cu.page == PG_SAVE) &&
        now - cu.t0 >= (uint32_t)HOLD_MS[settings_hold % 4u]) {
        if ((cu.armed == BT_EDIT || cu.armed == BT_METRO) && cu.ledit && cu.lock == L_LOOP) {   /* the loop editor:
                                                    * EDIT or SEQ held again closes it, as a layer's own button does (no
                                                    * engine picker, no METRO layer from there; a knob turned while the
                                                    * button is still down: nothing, cu_knob) */
            cu.open = 1;
            cu.hclosed = 0xFF;
            cu_ledit_close();
        } else
        if (cu.armed == BT_LOOP && cu.ledit && cu.lock == L_LOOP) {   /* .. LOOP held there: the PLAY menu (the owner,
                                                    * 2026-10-11: "from the Sequencer, holding the play key should
                                                    * enter the Play Menu, not return to home"): the editor closes, the
                                                    * menu stays, and a knob turned with LOOP still down is the menu's
                                                    * (PRESETS: the slot) */
            cu.open = 1;
            cu_ledit_close();
        } else
        if (cu.armed == BT_SAVE) {                 /* SAVE held alone past the hold time: the global save (a key, a
                                                    * knob or another button touched meanwhile: a combo, nothing).
                                                    * Its release does nothing. (The naming dialog: SAVE held 1 s asks
                                                    * to delete, above) */
            cu.open = 1;
            cu_gsave();
        } else
#if CR_EDIT_HOOKS
        if (cu.page == PG_EDIT && !cu_layer() && ce_owns(cu.armed)) {   /* the editor's: its hold, no layer */
            cu.open = 1;
            cu.combo = 1;
            ce_button(cu.armed, CE_HOLD);
        } else
#endif
        if (cu.lock && cu_layer_of(cu.armed) == cu.lock) {   /* a layer's own button held again past the hold time
                                                              * (alone: a key or a knob touched meanwhile is a combo in
                                                              * the layer, cu_activity): the layer closes, as it
                                                              * opened; the engine picker keeps its sound (as EDIT
                                                              * tapped). Its release does nothing */
            cu.open = 1;
            cu.hclosed = cu.lock;
            if (cu.lock == L_EDIT)
                cu_pick_end(1);
            cu_layer_close();
        } else {
            cu.open = 1;
            cu_opened(cu.armed);
        }
    }
    cu_scan_keys(ne, km);
#if BOARD_SMK37
    cu_smk_input(pe);
#endif
    if (cu.b3_pend && ((cu.kheld >> CU_KEY_LOCK) & 1u) && now - cu.b3_t0 >= (uint32_t)HOLD_MS[settings_hold % 4u]) {
        cu.b3_pend = 0;                            /* B3 held alone past the hold time: the LOCK layer (held again
                                                    * in it: closed); its release does nothing, LOCK as it was */
        cu_lock_menu();
    }
    cu_scan_step();
    cu_touch_poll();
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
/* as ui_input.c ui_leds: the picture built off-line, the glow (fm1_led_dim) first, one byte per column.
 * Four levels (hal/fm1_input.h; the owner's "20 / 50 / 75 / 100 %"): faint (nf -> fm1_led_faint: an idle chord key,
 * B3) < dim (nd -> fm1_led_dim: the glow: an idle button, a note of the scale, a layer's map, a loop's note) < mid
 * (nm -> fm1_led_dim2: the tonic) < lit (nl, own -> fm1_led) */
static uint8_t cu_led_pos[CU_LED_IDS];             /* (col << 3) | row bit, 0xFF = none (FM1_KEYMAP) */
#if BOARD_SMK37
/* SMK-37: no matrix of LEDs; the bitmaps are a plain id space (id 0..63 at column id / 8, bit id % 8), read back
 * by cu_smk_leds. PLAY's green: an id of its own */
#define LED_PLAY_GREEN ((7u << 3) | 4u)
static void cu_led_init(void)
{
    uint32_t id;
    for (id = 0; id < CU_LED_IDS; id++)
        cu_led_pos[id] = (uint8_t)id;
}
#else
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
#endif
static void cu_led(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = cu_led_pos[id % CU_LED_IDS];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}
#define CU_KEY_PC(k) ((53u + (k)) % 12u)          /* a key's note name (pitch class; OCT keeps it) */

#if BOARD_SMK37
/* The SMK-37's lights from cr_leds' bitmaps (ids: cu_led_init). The buttons: on or off (the SMK's button LEDs have
 * no glow), SEQ PLAY red with a loop in the slot, green while it plays. KNOB BANK lit with the view menu open, FADER
 * BANK on the bass part's sends, < and > where they page. The pads: the chord grid as the chord block's key LEDs
 * (lit, middle, glow, faint: four brightnesses), the chord types white, the extensions orange (the screen's
 * extension), LOCK white; the performance pads blue: the mode on bright, the mode picked while off at the middle
 * level, the others a glow. panic: everything blinks (white) */
static int cu_smk_bit(const uint8_t *m, uint32_t id)
{
    uint8_t q = cu_led_pos[id % CU_LED_IDS];
    return q != 0xFF && ((m[q >> 3] >> (q & 7u)) & 1u);
}
static void cu_smk_leds(const uint8_t *nl, const uint8_t *nd, const uint8_t *nm, const uint8_t *nf, int panic)
{
    static const uint8_t LV[5] = {0, 4, 14, 40, 110};   /* off, faint, glow, middle, lit (of 255 per colour unit) */
    uint32_t id, p, free = cu_smk_free(), play = panel.btn[BT_LOOP];
    for (id = 0; id < SMK_NLED; id++)
        fm1_led[id] = (uint8_t)(id < FM1_NBTN && cu_smk_bit(nl, id));
    if (play == 16u) {                             /* SEQ PLAY: green 16, red 18 */
        int green = cu_smk_bit(nl, LED_PLAY_GREEN);
        fm1_led[18] = (uint8_t)(fm1_led[16] && !green);
        fm1_led[16] = (uint8_t)green;
    }
    if (!panic) {
        if (free & 1u << SMK_BTN_KNOB_BANK)
            fm1_led[SMK_BTN_KNOB_BANK] = (uint8_t)(cu.lock == L_VIEW);
        if (free & 1u << SMK_BTN_FADER_BANK)
            fm1_led[SMK_BTN_FADER_BANK] = cu_fbank;
        for (id = SMK_BTN_LEFT; id <= SMK_BTN_RIGHT; id++)
            if (free & 1u << id)
                fm1_led[id] = (uint8_t)cu_smk_paging();
    }
    if ((int32_t)(padled_hold_until - fm1_ms) > 0)
        return;                                    /* (console padtest) */
    for (p = 0; p < 16u; p++) {
        uint32_t lv = 0, r = 255, g = 255, b = 255, fid;  /* white */
        int32_t k = CU_PAD_FN[p], i = CU_PAD_PERF[p];
        if (i >= 0) {                              /* a mode: bright while it plays, dim otherwise */
            lv = panic ? (cu_smk_bit(nl, CU_LK(0)) ? 4u : 0u) : cs.perform_on && cs.perf_sel == (uint32_t)i ? 4u : 2u;
            if (CU_PERF[i].mode == CR_PM_ARP || CU_PERF[i].mode == CR_PM_PATTERN) {   /* tempo-synced: green */
                r = 30;
                g = 200;
                b = 60;
            } else {                               /* free timing: cyan */
                r = 20;
                g = 170;
                b = 200;
            }
        } else if (k >= 0) {
            fid = CU_LK((uint32_t)k);
            lv = cu_smk_bit(nl, fid) ? 4u : cu_smk_bit(nm, fid) || cu_smk_bit(nd, fid) || cu_smk_bit(nf, fid) ? 2u : 0u;
            if ((uint32_t)k == CU_KEY_LOCK) {      /* blue */
                r = 40;
                g = 80;
            } else if (!((1u << CU_MOD_OF_KEY[k]) & CU_MOD_TOP)) {   /* 6 m7 M7 9: orange */
                g = 120;
                b = 25;
            }
        }
        padled_set(p, (uint8_t)(r * LV[lv] / 255u), (uint8_t)(g * LV[lv] / 255u), (uint8_t)(b * LV[lv] / 255u));
    }
}
#endif

static void cr_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0}, own[FM1_NCOL] = {0}, ld[FM1_NCOL] = {0}, nm[FM1_NCOL] = {0};
    uint8_t nf[FM1_NCOL] = {0};
    uint32_t k, b, now = cu_now(), blink = (uint32_t)cu_blink(now), l = cu_layer();
    uint32_t full = cu_full() && !cu_key_pick();   /* the Full keyboard: the chord block's keys are notes */
    uint32_t key0 = full ? 0u : cu_cb_end();       /* the first key that plays */
    uint32_t kend = cu_kend();                     /* .. the key after the last (Full K Shorts: F#5 G5 are switches) */
    uint32_t scale = cs.key_on ? cu_scale_bits() : 0u;
    uint32_t snapped = cs.key_on && cs.snap && cs.key_notes && !cs.leds;   /* Snap (0.22): the roots
                                                    * out of the scale play, and glow faint for it (Glow; Stock keeps
                                                    * its two levels: the scale's keys lit, the others dark) */
    const cr_snap_t *sn = &cr_snap;
    if (cu.msg.big == 1u && (int32_t)(cu.msg.until - now) > 0) {   /* panic: everything flashes */
#if BOARD_SMK37
        for (k = 0; k < NB; k++)
            cu_led(nl, panel.btn[k], (int)blink);
        for (k = 0; k < CU_NKEY; k++)
            cu_led(nl, CU_LK(k), (int)blink);
        cu_smk_leds(nl, nd, nm, nf, 1);
#else
        for (k = 0; k < 41u; k++)
            cu_led(nl, k < NB ? panel.btn[k] : k, (int)blink);
        for (k = 0; k < FM1_NCOL; k++) {
            fm1_led_dim[k] = 0;
            fm1_led_dim2[k] = 0;
            fm1_led_faint[k] = 0;
            fm1_led[k] = nl[k];
        }
#endif
        return;
    }
    /* buttons: on = active */
    cu_led(nl, panel.btn[BT_KEY], cs.key_on);
    cu_led(nl, panel.btn[BT_PERF], cs.perform_on);
    cu_led(nl, panel.btn[BT_FX], cs.fx_on);
    cu_led(nl, panel.btn[BT_BASS], cs.bass_on);
    if (!cu_fm1())
        cu_led(nl, panel.btn[BT_LATCH], cs.sticky || sn->latching);
    else if ((cs.sticky || sn->latching) && l != L_LOCK) {   /* FM-1's LFO: LOCK steady, LATCH pulsing (the owner,
                                                    * 2026-10-16): lit, middle, glow, middle, 300 ms each; with LOCK on
                                                    * too, lit and middle only (its layer open: it blinks, below) */
        uint32_t ph = (now / 300u) & 3u;
        if (cu.clock ? !(ph & 1u) : ph == 0u)
            cu_led(nl, panel.btn[BT_LATCH], 1);
        else if (cu.clock || ph != 2u)
            cu_led(nm, panel.btn[BT_LATCH], 1);
    } else
        cu_led(nl, panel.btn[BT_LATCH], cu.clock);
    cu_led(nl, panel.btn[BT_OPT], cu.opt_open || cu_shift() || (l == L_LOOP && cs.loop_keys && !cu.ledit) ||
                                      (l == L_KEY && cs.key_play));   /* (the PLAY menu: Keys = Loops; the KEY layer:
                                                                       * keys: play. Lit = not the default) */
    cu_led(nl, panel.btn[BT_EDIT], (cu.page == PG_EDIT || (cu.ledit && l == L_LOOP)) && blink);   /* blinks while the
                                                                       * editor is open (the sound's, the loop's) */
    cu_led(nl, panel.btn[BT_SAVE], cu.page == PG_SAVE);
    cu_led(nl, panel.btn[BT_METRO], l == L_LOOP && (!cu.ledit || blink));   /* SEQ's cycle: blinking with EDIT in the
                                                                       * loop editor, lit in the PLAY menu (one more
                                                                       * tap: the view), dark on the view. The click:
                                                                       * the PLAY menu's cell */
    {                                                    /* REC: blinks capturing, lit armed; LOOP: a loop */
        uint32_t cap = sn->lcap;
        cu_led(nl, panel.btn[BT_REC], cap == CRL_CAP_ARMED || cap == CRL_CAP_OD_ARMED || cap == CRL_CAP_STEP ||
                                          ((cap == CRL_CAP_COUNTIN || cap == CRL_CAP_REC || cap == CRL_CAP_OD) && blink));
        cu_led(nl, panel.btn[BT_LOOP], sn->lstate != CRL_EMPTY);
        if (sn->lstate == CRL_PLAYING)                   /* its green LED while playing */
            nl[LED_PLAY_GREEN >> 3] |= (uint8_t)(1u << (LED_PLAY_GREEN & 7u));
    }
    if (l && cu_lbtn(l) != NB) {                         /* the open layer's button blinks (LOCK's: B3 in Classic,
                                                          * below; LFO in FM-1) */
        uint8_t q = cu_led_pos[panel.btn[cu_lbtn(l)]];
        if (q != 0xFF)
            nl[q >> 3] &= (uint8_t)~(1u << (q & 7u));
        cu_led(nl, panel.btn[cu_lbtn(l)], (int)blink);
    }
#if CR_EDIT_HOOKS
    if (cu.page == PG_EDIT && !l)                        /* the editor's buttons (EDIT blinking, the section lit) */
        ce_leds(nl, blink);
#endif
    if (sn->lcap == CRL_CAP_STEP && (!l || l == L_LOOP) && !cu.opt_open && cu.page == PG_NONE) {
        cu_led(nl, panel.btn[B_OCTDN], 1);               /* step entry: OCT- back, OCT+ a rest */
        cu_led(nl, panel.btn[B_OCTUP], 1);
    } else if (cu_picker_ctx() || l) {                   /* a picker: OCT- back (lit), OCT+ OK / close (blinking) */
        cu_led(nl, panel.btn[B_OCTDN], 1);
        cu_led(nl, panel.btn[B_OCTUP], (int)blink);
    } else {
        cu_led(nl, panel.btn[B_OCTDN], cu.octave < 0);
        cu_led(nl, panel.btn[B_OCTUP], cu.octave > 0);
    }
    for (b = 0; b < NB; b++)
        cu_led(nd, panel.btn[b], 1);
    /* the chord block: lit while held / latched (LOCK's latch: posted down, so the engine's held); B3 = LOCK, lit on.
     * Idle (the owner, 2026-10-12): the faint level (nf), under the glow of the scale's notes and of the buttons, so
     * the chord block reads as one quiet group and what is on in it stands out. Stock (two levels): idle = lit, as
     * it was (the last loop).
     * In the KEY layer (the one exception to "these LEDs are the chord keys' own state", the owner's, 2026-10-10)
     * they are its scale keys: MAJ and MIN glow at half, the selected scale's at full, the six without a job there
     * dark. Glow: half = fm1_led_dim, full = fm1_led. Stock has two levels only (it lights the idle keys, and a
     * layer's selected key through `own`): the selected scale's key lit, the other dark. With the layer's keys set
     * to play (cu_key_pick 0) they are the chord keys' own state again */
    for (k = 0; k < cu_cb_end() && !full; k++) {  /* (the Full keyboard: they are notes, lit with the roots below) */
        if (cu_mod_of(k) == 0xFFu) {               /* (Classic's B3, LOCK) */
            if (cu_full())                         /* (the KEY layer on pick, the Full keyboard: no LOCK) */
                continue;
            cu_led(nl, CU_LK(k), l == L_LOCK ? (int)blink : cu.clock);   /* (its layer open: it blinks, as a layer's
                                                                         * button; the label says on / off) */
            cu_led(nf, CU_LK(k), 1);
            continue;
        }
        if (cu_key_pick()) {
            if (k == CU_KEY_MIN || k == CU_KEY_MAJ) {
                if ((k == CU_KEY_MIN) == (cs.scale == CR_SCALE_MINOR))
                    cu_led(own, CU_LK(k), 1);
                else if (!cs.leds)
                    cu_led(nd, CU_LK(k), 1);
            }
            continue;
        }
        if (cu_fm1() && ((CU_TK_MASK >> k) & 1u))   /* FM-1's type keys: held, or latched (SUS: the pair lit) */
            cu_led(nl, CU_LK(k), (int)(((cu.tk_held | cu_tk_keys_of(cu.mlatch)) >> k) & 1u));
        else
            cu_led(nl, CU_LK(k), (int)((sn->mods_active >> cu_mod_of(k)) & 1u));
        cu_led(nf, CU_LK(k), 1);
    }
    if (cu_fshort() && !cu_key_pick()) {           /* Full K Shorts: F#5 and G5, switches: faint off, lit on */
        cu_led(cu.full_chk ? nl : nf, CU_LK(25u), 1);
        cu_led(cu.full_all ? nl : nf, CU_LK(26u), 1);
    }
    /* the roots: a layer's map, or the voiced notes where they sound */
    if (l == L_LOOP && cs.loop_keys && !cu.ledit) {   /* the slots: lit = a loop, blinking = the one selected (the
                                                    * PLAY menu with Keys = Play, the loop editor: the keys as on
                                                    * the view) */
        uint32_t sel = cu_lm_slot();
        for (k = CU_PICK0; k < CU_NKEY; k++) {
            int32_t wi = cu_white_idx(k);
            if (wi < 0 || wi >= (int32_t)CRL_SLOTS) {
                if (k == 10u || k == 13u)          /* CLEAR, UNDO */
                    cu_led(nl, CU_LK(k), 1);
                continue;
            }
            cu_led(nd, CU_LK(k), 1);
            if ((uint32_t)wi == sel ? blink : (cs.loop_used >> wi) & 1u)
                cu_led(own, CU_LK(k), 1);
        }
    } else if (l == L_FX && cs.fx_page == CU_FXPG_ROUTE) {   /* the Routing page: its shortcuts (the LEDs have
                                                    * no colour: the four blocks' keys glow, A#4 at the middle level,
                                                    * the held one lit) */
        for (k = CU_ROOT0_FM1; k < CU_NKEY; k++) {
            int32_t b = cu_rt_block_of(k);
            if (b == (int32_t)XFE_N)
                cu_led(nm, CU_LK(k), 1);
            else if (b >= 0)
                cu_led(cu_rt.key == k + 1u ? own : nd, CU_LK(k), 1);
        }
    } else if (cu_layer_keys(l) && l != L_KEY) {
        int32_t sel = cu_layer_sel(l), n = cu_layer_count(l);
        for (k = CU_PICK0; k < CU_NKEY; k++) {
            int32_t wi = cu_white_idx(k);
            if (wi >= 0 && wi < n)
                cu_led(wi == sel ? own : nd, CU_LK(k), 1);
        }
    } else if (l == L_KEY && !cs.key_play) {             /* keys: pick: every root picks a tonic. Key Mode off: they
                                                          * all glow; on: the key as everywhere (below): its notes
                                                          * glow, the tonic at the middle level (Stock: lit, as a
                                                          * picker's selection), the roots out of it dark */
        for (k = CU_PICK0; k < CU_NKEY; k++) {
            uint32_t pc = (53u + k) % 12u;
            if (!cs.key_on)
                cu_led(nd, CU_LK(k), 1);
            else if (pc == cs.tonic)
                cu_led(cs.leds ? own : nm, CU_LK(k), 1);
            else if ((scale >> pc) & 1u)
                cu_led(nd, CU_LK(k), 1);
            else if (snapped)
                cu_led(nf, CU_LK(k), 1);
        }
    } else if (cu.page == PG_SAVE && ce.save_step) {     /* naming: the keys that type lit (the letters, D#4 space) */
        for (k = CU_PICK0; k < CU_NKEY; k++)
            if (cu_white_idx(k) >= 0 || k == 10u || k == 13u)
                cu_led(own, CU_LK(k), 1);
    } else {                                             /* (Options too: the playing state, the OPT LED says it is open) */
        /* A sounding note lights every key that plays of its note name (the owner, 2026-10-11): a chord voiced,
         * transposed or shifted out of the keys' two octaves still shows, on both of them. Until then a note lit the
         * one key of its pitch, so a chord out of range lit nothing. pcs: the note names that sound (bit 0 = C), from
         * the snapshot's shown chord (ci: the player's last chord as voiced, with the KEY layer's Transpose: what
         * sounds, not the key under the finger); lpcs: a playing loop's (lnote). The keys that play: the 18 roots,
         * all 27 on the Full keyboard; never a chord key or B3 in the Chords layout (their LEDs are their own state) */
        uint32_t i, pcs = 0, lpcs = 0;
        if (sn->ci.sounding && !sn->ldisp)              /* the player's notes (the loop's glow below) */
            for (i = 0; i < sn->ci.nnotes; i++)
                pcs |= 1u << (sn->ci.notes[i] % 12u);
        for (k = 0; full && k < kend; k++)            /* the Full keyboard: every key held that plays a note (the
                                                          * chord shown is the last one only) */
            if (cu.key_note[k] && cu.key_note[k] != 0xFFu && (!cs.key_on || (cs.key_notes && ((scale >> CU_KEY_PC(k)) & 1u))))
                pcs |= 1u << ((uint32_t)(cu.key_note[k] - 1 + 48 + cs.transpose) % 12u);
                                                     /* (Key Mode, Notes: a key out of the scale plays nothing, or
                                                      * Snap's note, shown by ci: dark; Chords: its chord, by ci) */
        if (sn->perform_on && sn->perf_pos >= 0 && sn->ci.sounding && !blink)
            pcs &= ~(1u << (sn->perf_note % 12u));       /* the performed note blinks (on every key of its name) */
        for (k = key0; k < kend; k++)
            cu_led(nl, CU_LK(k), (int)((pcs >> CU_KEY_PC(k)) & 1u));
        for (i = 0; i < 16u; i++) {                      /* (no loop voice sounds: sixteen zero bytes) */
            uint32_t m = sn->lnote[i], n;
            for (n = i * 8u; m; m >>= 1, n++)
                if (m & 1u)
                    lpcs |= 1u << (n % 12u);
        }
        /* the keys that play. Key Mode off (the owner, 2026-10-12): no idle glow at all: dark unless they sound
         * (nl above) or a loop's note glows on them (ld below); the Full keyboard: all 27 so. Until then they all
         * glowed (the low level); Stock keeps that (its idle keys lit: its two levels are the stock firmware's
         * picture, not the glow's). Key Mode on (the owner, 2026-10-11): the
         * key shows on them: a note of the scale glows (low), the tonic's keys at the middle level (fm1_led_dim2),
         * a key out of the scale is dark, or glows faint when Snap makes it play (0.22); a sounding note is lit over
         * it (nl). Until then only the black keys out of the scale went dark: every white key glowed whatever the
         * key. Stock has two levels: below, the scale's keys lit (the tonic as the rest), the others dark */
        for (k = key0; k < kend; k++) {
            uint32_t pc = CU_KEY_PC(k);                  /* (the octave shift keeps the pitch class) */
            if (!cs.key_on)
                cu_led(nd, CU_LK(k), (int)cs.leds);
            else if (pc == cs.tonic)
                cu_led(nm, CU_LK(k), 1);
            else if ((scale >> pc) & 1u)
                cu_led(nd, CU_LK(k), 1);
            else if (snapped)
                cu_led(nf, CU_LK(k), 1);
        }
        for (k = key0; k < kend; k++)                 /* PLAN 6: the loop's notes glow dim (not the player's), on
                                                          * every key of their names too */
            cu_led(ld, CU_LK(k), (int)((lpcs & ~pcs) >> CU_KEY_PC(k)) & 1);
    }
    for (k = 0; k < FM1_NCOL; k++) {
        if (cs.leds) {                                   /* STOCK: the idle ones lit, the active ones dark (two
                                                          * levels: the middle and the faint one are "idle" too) */
            nl[k] = (uint8_t)((nd[k] | nm[k] | nf[k]) & ~nl[k]);
            nd[k] = nm[k] = nf[k] = 0;
        }
        nl[k] |= own[k];
        nd[k] |= ld[k];
        nl[k] &= (uint8_t)~ld[k];
    }
#if BOARD_SMK37
    cu_smk_leds(nl, nd, nm, nf, 0);
#else
    for (k = 0; k < FM1_NCOL; k++)
        fm1_led_dim[k] = nd[k];
    for (k = 0; k < FM1_NCOL; k++)
        fm1_led_dim2[k] = (uint8_t)(nm[k] & ~nl[k]);
    for (k = 0; k < FM1_NCOL; k++)
        fm1_led_faint[k] = (uint8_t)(nf[k] & ~nd[k]);
    for (k = 0; k < FM1_NCOL; k++)
        fm1_led[k] = nl[k];
#endif
}

/* ------------------------------------------------------- the view-model --- */
static const uint8_t CU_TRIAD[CR_Q_COUNT][3] = {     /* intervals of each quality's base (the rest: extensions) */
    {0, 0, 0}, {0, 3, 6}, {0, 3, 7}, {0, 4, 7}, {0, 5, 7}, {0, 4, 8}, {0, 7, 7}, {0, 3, 5}};

_Static_assert(sizeof ((cr_name_t *)0)->sup >= CR_SUP_MAX && sizeof ((cr_name_t *)0)->quality >= 5,
               "the screen's name holds the engine's whole (cr_chord_name)");
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

/* the KEY layer's Numerals (0.22): the sounding chord's Roman numeral in the key (Solfege, GitHub issue 15: the same
 * degree as a syllable, cr_chord_solfege), "" when there is none to show (the option or Key Mode off, nothing
 * sounding). The root is the one PLAYED: the name's (cr_chord_info: Key Mode resolved, transposed) less the Transpose
 * inside it, so the tonic chord reads I (DO) at any Transpose (the owner, 2026-10-12; with Transpose +1 in C the name
 * says C#M and the numeral I) */
static void cu_numeral(char b[CR_NUMERAL_MAX])
{
    const cr_chord_info_t *ci = &cr_snap.ci;
    b[0] = 0;
    if (cs.numerals && cs.key_on && ci->valid && ci->sounding)
        (cs.numerals == 2u ? cr_chord_solfege : cr_chord_numeral)((uint8_t)(((int)ci->root_pc - ci->transpose) % 12 + 12),
                                                                 ci->quality, cs.tonic, cs.scale, b);
}
/* .. on the Chord view, blue, centred above the chord name (cr_draw.c cr_p_chord, cr_p_arp). numeral_on is the
 * layout (the arp's name sits lower to make room): set by the two settings alone, not by a chord sounding, so the
 * name does not move as chords are struck and released */
_Static_assert(sizeof ((cr_screen_t *)0)->numeral >= CR_NUMERAL_MAX, "the screen's numeral holds the engine's");
static void cu_numeral_put(cr_screen_t *s)
{
    if (!cs.numerals || !cs.key_on) return;
    s->numeral_on = 1;
    cu_numeral(s->numeral);
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
    if (cs.single == CR_SINGLE_SPLIT_BASS && !sn->perform_on) {   /* Split Bass: the bass is played by hand (Bass on /
                                                    * off and its Behaviour wait, ignored) */
        cu_cpy(s->right, "Split Bass", sizeof s->right);
        s->right_col = CR_COL_ORANGE;
    } else if (sn->bass_on && cs.bass_mode == CR_BASS_SOLO && cs.single != CR_SINGLE_SPLIT_BASS) {
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
    else if (cu.clock && (!cu_full() || cu_fm1()))   /* LOCK on (mock-up 3): a small white "lock" (Classic's Full
                                                   * keyboard: no LOCK there, the latch waits; FM-1's LOCK is on LFO) */
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
#error "cr_ui.c: FELUCCA_VERSION is the unit's (choralroot.c, tools/emu/build.sh): no version literal here"
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
 * update area) and reboot; the boot after it loads nothing (an
 * erased flash: factory sounds, default settings, calibration from RAM or the default). The FM-1's regions (SMK-37:
 * storage.c ST_SMK_REGIONS, the same as a new owner clears): the stores and
 * FM6 bank copy A 0x97000..0x9FFFF (storage.c st_sector: VA store, FM6 stores, the project slots, 0x9F000), the loop
 * slots 0xC8000..0xDBFFF (CRL_FL_BASE), the user sound banks 0xDC000..0xDFFFF, the settings A / B and FM6 bank
 * copy B 0xFC000..0xFEFFF; FELUCCA_CZ: the CZ-1 tone store's second half and the eight CZ-1 banks 0xA0000..0xB1FFF;
 * FELUCCA_QUAD: the QUAD patch store 0xB2000..0xB3FFF, the session 0xB4000..0xB5FFF (0.18) and the favourites
 * 0xB6000..0xB7FFF (0.22: marks on sounds that are gone would name whatever takes their slots); UP_POOL: the sound
 * pool 0xB8000..0xC7FFF (storage.c sp_*, the user sounds since 0.22: until 0.24-beta2 it was left out, so the sounds
 * stayed). On a full build that is all of 0x97000..0xDFFFF and 0xFC000..0xFEFFF, 76 sectors; the asserts below
 * tie the list to storage.c's map */
#ifndef CR_REBOOT
#define CR_REBOOT() do { bootguard_settled(&bootguard); usb_detach(); fm1_delay_ms(30); fm1_reboot(); } while (0)
#endif
static cr_screen_t cu_scr __attribute__((section(".pool")));   /* (cr_ui_draw's, defined below) */
static void cu_flash_erase(void)
{
#if BOARD_SMK37
#define R ST_SMK_REGIONS                           /* (SMK-37: storage.c, the same as a new owner clears) */
#else
    static const uint32_t R[][2] = {{0x97000u, 9u}, {CRL_FL_BASE, 2u * CRL_SLOTS}, {0xDC000u, 4u}, {0xFC000u, 3u},
#if FELUCCA_CZ
                                    {0xA0000u, 18u},   /* (storage.c: OBJ_CZSTORE1, OBJ_CZBANK0..+7) */
#endif
#if FELUCCA_QUAD
                                    {0xB2000u, 6u},    /* (storage.c: OBJ_QUADSTORE, OBJ_SESSION, OBJ_FAVS) */
#endif
#if UP_POOL && FELUCCA_FLASH
                                    {SP_BASE, SP_NSEC},   /* (storage.c sp_*: the sound pool's 16 sectors) */
#endif
    };
#if FELUCCA_CZ && FELUCCA_QUAD && FELUCCA_FLASH
    _Static_assert(OBJ_COUNT == 20u && OBJ_FAVS + 1u == OBJ_COUNT, "Flash Data: a new storage object needs a place here");
#endif
#if UP_POOL && FELUCCA_FLASH
    _Static_assert(SP_BASE == 0xB2000u + 6u * ST_SECTOR && SP_BASE + SP_NSEC * ST_SECTOR == CRL_FL_BASE,
                   "Flash Data: the pool lies between the favourites and the loop slots");
#endif
#endif
    cr_screen_t *es = &cu_scr;                     /* (the frame's screen, in the pool: off the stack and out of
                                                    * RAM; after this comes the reboot) */
    uint32_t r, k, n = 0, total = 0, bad = 0;
    for (r = 0; r < NELEM(R); r++)
        total += R[r][1];
    for (r = 0; r < NELEM(R); r++)
        for (k = 0; k < R[r][1]; k++) {
            if (!(n % 4u)) {                       /* the progress, every 4 sectors (~180 ms) */
                cr_screen_clear(es);
                es->kind = CR_K_BIG;
                cu_cpy(es->value, "ERASING", sizeof es->value);
                es->size = 52;
                es->block = CR_COL_RED;
                cu_int(es->sub, (int32_t)(n * 100u / total), 0, sizeof es->sub);
                cu_cat(es->sub, " %", sizeof es->sub);
                cu_cpy(es->label, "flash data: do not switch off", sizeof es->label);
                cr_draw_invalidate();
                cr_draw(es, CR_ANIM_SETTLED);
            }
            fm1_wdt_feed();
#if FELUCCA_FLASH
            if (flash_ok && st_erase(R[r][0] + k * ST_SECTOR))
                bad++;
#endif
            n++;
        }
#if BOARD_SMK37
#undef R
#endif
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
        cu_cpy(s->foot, CR_VERSION, sizeof s->foot);
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
    if (cu.kpage) {                                /* page 2 (SELECT): Key plays, Keyboard, Snap, Numerals */
        s->cell[0][0].flags = s->cell[0][1].flags = s->cell[0][3].flags = CR_CF_ON;
        s->cell[0][2].flags = (uint8_t)(CR_CF_ON | (cs.key_notes ? 0u : CR_CF_DIM));   /* (Snap changes nothing
                                                    * with Key plays Chords, on either keyboard: grey) */
        cu_cpy(s->cell[0][2].label, "Snap", sizeof s->cell[0][2].label);
        cu_cpy(s->cell[0][2].value, CU_SNAP[cs.snap % 3u], sizeof s->cell[0][2].value);
        cu_cpy(s->cell[0][3].label, "Numerals", sizeof s->cell[0][3].label);
        cu_cpy(s->cell[0][3].value, CU_NUMS[cs.numerals % 3u], sizeof s->cell[0][3].value);
        cu_cpy(s->cell[0][0].label, "Key plays", sizeof s->cell[0][0].label);
        cu_cpy(s->cell[0][0].value, cs.key_notes ? "Notes" : "Chords", sizeof s->cell[0][0].value);
        cu_cpy(s->cell[0][1].label, "Keyboard", sizeof s->cell[0][1].label);
        cu_cpy(s->cell[0][1].value, cs.kbd_full ? "Full" : "Chords", sizeof s->cell[0][1].value);
        cu_hot_row(s, L_KEY);
        return;
    }
    for (k = 0; k < 4u; k++) {
        s->cell[0][k].flags = CR_CF_ON;
        cu_cpy(s->cell[0][k].label, LB[k], sizeof s->cell[0][k].label);
    }
    cu_cpy(s->cell[0][0].value, cs.key_on ? CU_NOTE[cs.tonic % 12u] : "-", sizeof s->cell[0][0].value);
    cu_cpy(s->cell[0][1].value, cs.scale ? "Minor" : "Major", sizeof s->cell[0][1].value);
    cu_int(s->cell[0][2].value, cs.transpose, 1, sizeof s->cell[0][2].value);
    s->cell[0][2].glyph = CR_G_SHIFT;
    s->cell[0][2].pct = (uint8_t)((uint32_t)(cs.transpose + 24) * 255u / 48u);
    cu_cpy(s->cell[0][3].value, cs.single == 2u ? "Split Bass" : cs.single ? "Split Oct" : "Full",
           sizeof s->cell[0][3].value);
    cu_hot_row(s, L_KEY);
}
/* the PLAY menu's knob row (docs/LOOPER-MODES.md), two pages (SELECT). Page 1: Length (the next take's: Free, 1 .. 16
 * bars), Rate (x1/16 .. x4), Quantize (echoes), Count-in (gate: full on, narrow off); page 2: Level (bar), Metronome
 * (gate, On / Off), KNOB 3 / 4 free. Until 0.17: Quantize Count-in Level Keys, the length on SELECT; Keys is GLO's alone now */
static void cu_loop_cells(cr_screen_t *s)
{
    static const char *const LB[4] = {"Length", "Rate", "Quantize", "Count-in"};
    uint32_t k, q = cs.loop_quant % CRL_NQUANT;
    if (cu.lpage) {
        s->cell[0][0].flags = CR_CF_ON;
        cu_cpy(s->cell[0][0].label, "Level", sizeof s->cell[0][0].label);
        cu_int(s->cell[0][0].value, cs.loop_level, 0, sizeof s->cell[0][0].value);
        s->cell[0][0].glyph = CR_G_BAR;
        s->cell[0][0].pct = (uint8_t)(cs.loop_level * 255u / 100u);
        s->cell[0][1].flags = CR_CF_ON;            /* the click, as Count-in on page 1 */
        cu_cpy(s->cell[0][1].label, "Metronome", sizeof s->cell[0][1].label);
        cu_cpy(s->cell[0][1].value, cs.metro ? "On" : "Off", sizeof s->cell[0][1].value);
        s->cell[0][1].glyph = CR_G_GATE;
        s->cell[0][1].pct = cs.metro ? 255 : 26;
        cu_hot_row(s, L_LOOP);
        return;
    }
    for (k = 0; k < 4u; k++) {
        s->cell[0][k].flags = CR_CF_ON;
        cu_cpy(s->cell[0][k].label, LB[k], sizeof s->cell[0][k].label);
    }
    cu_cpy(s->cell[0][0].value, CU_LOOPLEN[cs.loop_len % CRL_NSYNC], sizeof s->cell[0][0].value);
    cu_cpy(s->cell[0][1].value, CU_RATE[cs.loop_rate % CRL_NRATE], sizeof s->cell[0][1].value);
    cu_cpy(s->cell[0][2].value, CU_QUANT[q], sizeof s->cell[0][2].value);
    s->cell[0][2].glyph = CR_G_ECHOES;              /* "none": no grid (pct 0, pct2 0: the baseline alone, cr_draw.c);
                                                     * 1/4 .. 1/32 the repeats, the spacing growing as before */
    s->cell[0][2].pct = (uint8_t)(q * 255u / (CRL_NQUANT - 1u));
    s->cell[0][2].pct2 = q ? 255u : 0u;
    if (cs.loop_mode % CRL_NMODE == CRL_MODE_STEP) {   /* Step: its steps are sixteenths whatever Quantize is set to
                                                     * (a take's grid): the cell says so, dim, and KNOB 3 rests */
        cu_cpy(s->cell[0][2].value, "1/16", sizeof s->cell[0][2].value);
        s->cell[0][2].pct = (uint8_t)(4u * 255u / (CRL_NQUANT - 1u));
        s->cell[0][2].pct2 = 255u;
        s->cell[0][2].flags = CR_CF_ON | CR_CF_DIM;
    }
    cu_cpy(s->cell[0][3].value, cs.loop_count_in ? "On" : "Off", sizeof s->cell[0][3].value);
    s->cell[0][3].glyph = CR_G_GATE;
    s->cell[0][3].pct = cs.loop_count_in ? 255 : 26;
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
        uint32_t g = CRL_STEP, cur = (uint32_t)crl.step * g;
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
    static const char *const ED[2] = {"Step", "Trig"};
    char b[12];
    if (cu.ledit)                                  /* the loop editor: its page's name where the record mode is (the
                                                    * picker's marks: the two pages, SELECT) */
        cu_picker(s, ED, 2, cu.le_page ? 1 : 0, CR_COL_WHITE, "", "");
    else
        cu_picker(s, CU_LOOP_MODE, CRL_NMODE, cs.loop_mode % CRL_NMODE, CR_COL_WHITE, "", "");
    s->kind = CR_K_LOOPMENU;
    s->orient = 1;
    if (cu.ledit) {
        cu_le_sync();
        cu_ledit_cells(s);
    } else
        cu_loop_cells(s);
    cu_row_trace(s, "loop", seen, sizeof seen);
    s->lm_dirty = (uint8_t)(sn->ldirty ? cs.loop_slot + 1u : 0u);   /* changes not saved yet (SAVE held saves) */
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
    if (cu.ledit) {                                /* the loop editor: the pattern's steps (a bar a row, four rows a
                                                    * page), filled where a note starts, dim where a long note still
                                                    * sounds, the cursor on the selected step, the playhead while the
                                                    * loop plays; its chord and "step 3/16" under it */
        uint32_t per = cu_le_per(), pg = cu_le_page(), first = cu_le_map.first, n, ph;
        char r[4], q[5], sup[CR_SUP_MAX];
        n = cu_le_map.nstep > first ? cu_le_map.nstep - first : 0u;
        n = n > pg ? pg : n;
        s->lm_mid = CR_LM_STEPS;
        s->lm_bars = (uint8_t)((n + per - 1u) / per ? (n + per - 1u) / per : 1u);
        s->lm_beats = (uint8_t)per;
        s->lm_lane[0] = cu_le_map.start;
        s->lm_lane[1] = cu_le_map.tail;
        s->lm_lane[3] = cu_le_map.rat[0];          /* (the ratchet's ticks) */
        s->lm_lane[4] = cu_le_map.rat[1];
        for (ph = 0; ph < sizeof s->lm_cell; ph++) /* (the velocity's colour, the chance / condition strip) */
            s->lm_cell[ph] = cu_le_map.cell[ph];
        s->lm_ed = 1;
        if (cu_le.cnt) {                           /* the selected step's note: the white bar from its own cell (a part
                                                    * of it: Note len under a step) over the steps it sounds on into,
                                                    * across the loop's end as the note does */
            uint32_t ns = cu_le_map.nstep, e = cu_le.c0 + (cu_le.nf ? 1u : cu_le.n);
            s->lm_bar = (uint8_t)(cu_le.nf ? cu_le.nf : 4u);
            s->lm_lane[2] = cu_le_span(cu_le.c0 + 1u, e < ns ? e : ns, first, n);
            if (e > ns)
                s->lm_lane[2] |= cu_le_span(0u, e - ns < cu_le.c0 ? e - ns : cu_le.c0, first, n);
        }
        s->lm_steps = (uint8_t)n;
        s->lm_pos = (uint16_t)(((cu_le.c0 - first) & 63u) | (cu_blink(now) ? 0u : CR_LM_CUR_OFF));
        ph = sn->lpos / CRL_STEP;
        if (sn->lstate == CRL_PLAYING && ph >= first && ph < first + n)
            s->lm_pos |= CR_LM_PLAY(ph - first);
        /* the name line: ALWAYS the selected step's (the owner, 2026-10-11: "the name of the chord changes during
         * playback, I would like this to always show chord/notes of the selected step"), but while a root key is DOWN:
         * then the chord the player plays into it (his own, never a loop voice's: until then ci, the engine's "most
         * recent voice", which is the loop's whenever the loop strikes and a latched chord's whenever it does not) */
        if ((cu.kheld >> cu_root0()) && sn->pl_on) {
            cr_chord_name(sn->pl_pc, sn->pl_q, sn->pl_x, r, q, sup);
            cu_cpy(s->sub, r, sizeof s->sub);
            cu_cat(s->sub, q, sizeof s->sub);
            cu_cat(s->sub, sup, sizeof s->sub);
        } else if (cu_le.cnt) {
            cr_chord_name((uint8_t)(cu_le.root % 12u), (uint8_t)(cu_le.qx >> 4), (uint8_t)(cu_le.qx & 15u), r, q, sup);
            cu_cpy(s->sub, r, sizeof s->sub);
            cu_cat(s->sub, q, sizeof s->sub);
            cu_cat(s->sub, sup, sizeof s->sub);
            if (cu_le.voic > 0 && cu_le.ch) {   /* a fixed voicing, blue after the chord (Follow: nothing; a
                                                    * single note or a bass-side note is not voiced: nothing) */
                cu_cat(s->sub, "\t", sizeof s->sub);
                cu_le_voic_txt(b, sizeof b);
                cu_cat(s->sub, b, sizeof s->sub);
            }
        } else
            cu_cpy(s->sub, "-", sizeof s->sub);
        /* while the loop plays: what it struck last, at the left of the page's name (cr_draw.c cr_lm_now): the chord
         * or the note, no voicing; it stays until the next step that sounds (a step alone in a bar would flash for a
         * sixteenth), nothing before the first one and nothing when stopped */
        s->lm_now[0] = 0;
        if (sn->lstate == CRL_PLAYING && sn->lhit) {
            cr_chord_name((uint8_t)(sn->lhit_root % 12u), (uint8_t)(sn->lhit_qx >> 4), (uint8_t)(sn->lhit_qx & 15u), r, q, sup);
            cu_cpy(s->lm_now, r, sizeof s->lm_now);
            cu_cat(s->lm_now, q, sizeof s->lm_now);
            cu_cat(s->lm_now, sup, sizeof s->lm_now);
        }
        cu_cpy(s->value, "step ", sizeof s->value);
        cu_int(b, (int32_t)cu_le.c0 + 1, 0, sizeof b);
        cu_cat(s->value, b, sizeof s->value);
        cu_cat(s->value, "/", sizeof s->value);
        cu_int(b, (int32_t)cu_le.nstep, 0, sizeof b);
        cu_cat(s->value, b, sizeof s->value);
        if (cu_le.nstep > pg) {                    /* (a take longer than a page: which one) */
            cu_cat(s->value, " \267 p", sizeof s->value);
            cu_int(b, (int32_t)(first / pg) + 1, 0, sizeof b);
            cu_cat(s->value, b, sizeof s->value);
        }
        cu_cpy(s->foot, cu.le_page ? "2/2 \267 PRESETS: step \267 REC: clear \267 SEQ: back"
                                   : "1/2 \267 PRESETS: step \267 REC: clear \267 SEQ: back", sizeof s->foot);
        return;
    }
    if (cap == CRL_CAP_STEP) {                     /* step entry: the pattern's steps, sixteenths, a bar a row, four
                                                    * rows a page (64 steps: the whole pattern in 4/4). A set Length:
                                                    * all its steps; Free: the bars reached so far */
        uint32_t per = bar / CRL_STEP, pg = CRL_STEPS / per * per, nst, first, shown, full;
        char r[4], q[5], sup[CR_SUP_MAX];
        full = !sn->lstep_len && sn->lstep >= CRL_STEPS;   /* (Free: past the 64th: nothing more goes in) */
        nst = sn->lstep_len ? sn->lstep_len / CRL_STEP : full ? CRL_STEPS
                            : (sn->lstep_end > sn->lstep + 1u ? sn->lstep_end : sn->lstep + 1u);
        first = (full ? CRL_STEPS - 1u : sn->lstep) / pg * pg;
        shown = (nst - first + per - 1u) / per;
        shown = shown < 1u ? 1u : shown > pg / per ? pg / per : shown;
        cu_lm_marks(first, per * shown, CR_LM_STEPS);
        s->lm_mid = CR_LM_STEPS;
        s->lm_bars = (uint8_t)shown;
        s->lm_beats = (uint8_t)per;
        s->lm_lane[0] = cu_lmc.lane[0];
        s->lm_steps = (uint8_t)(sn->lstep_len && nst - first < per * shown ? nst - first : 0u);
        s->lm_pos = (uint16_t)(((sn->lstep - first) & 63u) | (full || !cu_blink(now) ? CR_LM_CUR_OFF : 0u));
        if (sn->ci.sounding && !sn->ldisp) {       /* the chord being entered, else the one at the cursor */
            cu_cpy(s->sub, sn->ci.root, sizeof s->sub);
            cu_cat(s->sub, sn->ci.qual, sizeof s->sub);
            cu_cat(s->sub, sn->ci.sup, sizeof s->sub);
        } else if (cu_lmc.cur_ok && !full) {
            cr_chord_name((uint8_t)(cu_lmc.cur_root % 12u), (uint8_t)(cu_lmc.cur_qx >> 4), (uint8_t)(cu_lmc.cur_qx & 15u),
                          r, q, sup);
            cu_cpy(s->sub, r, sizeof s->sub);
            cu_cat(s->sub, q, sizeof s->sub);
            cu_cat(s->sub, sup, sizeof s->sub);
        } else
            cu_cpy(s->sub, "-", sizeof s->sub);
        if (full)                                  /* "64 steps \267 full"; "step 5/32" (a set Length); "step 5" (Free) */
            cu_cpy(s->value, "64 steps \267 full", sizeof s->value);
        else {
            cu_cpy(s->value, "step ", sizeof s->value);
            cu_int(b, (int32_t)sn->lstep + 1, 0, sizeof b);
            cu_cat(s->value, b, sizeof s->value);
            if (sn->lstep_len) {
                cu_cat(s->value, "/", sizeof s->value);
                cu_int(b, (int32_t)nst, 0, sizeof b);
                cu_cat(s->value, b, sizeof s->value);
            }
        }
        cu_cpy(s->foot, "OCT-: back \267 OCT+: rest \267 REC: done", sizeof s->foot);
        return;
    }
    /* the bottom line, as the KEY layer's label and footer: the page (SELECT), what the keys do, what GLO makes them */
    cu_cpy(s->foot, cu.lpage ? "2/2" : "1/2", sizeof s->foot);
    cu_cat(s->foot, cs.loop_keys ? " \267 keys: loops \267 GLO: play" : " \267 keys: play \267 GLO: loops", sizeof s->foot);
    if (cap == CRL_CAP_REC || cap == CRL_CAP_OD || cap == CRL_CAP_OD_ARMED || sn->lstate == CRL_PLAYING) {
        uint32_t pos = sn->lpos, start = 0, shown, len;
        s->lm_mid = CR_LM_TIMELINE;
        s->lm_beats = (uint8_t)beats;
        if (cap == CRL_CAP_REC && !sn->lrec_len) { /* Free, the first take: the bars as they pass, the current one right */
            uint32_t nb = pos / bar + 1u, ms = pos * 30000u / ((cs.bpm ? cs.bpm : 120u) * cr_loop_rate_q48(&crl));
                                                   /* (60000 / 96 = 625 ms a tick at 1 BPM; the loop's ticks run at the
                                                    * Rate, in 48ths; pos <= CRL_MAX_LEN: 1.97e9 at most) */
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

static int cu_arp_layer(uint32_t now);
static void cu_layer_screen(cr_screen_t *s, uint32_t l)
{
    static const char *const PERF_ITEMS[7] = {"Strum", "Strum 2 Octaves", "Slop", "Arpeggiate", "Arp 2 Octaves",
                                              "Pattern", "Harp"};
    static const char *const FX_ITEMS[CU_NFX + 3u] = {"Sends", "Reverb", "Chorus", "Delay", "Drive", "Filter",
                                                      "Routing"};
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
        cu_cpy(s->label, cs.key_play ? "key \267 keys play" : "key \267 keys pick", sizeof s->label);   /* (as "perform
                                                                                                    * \267 on") */
        cu_cat(s->label, cu.kpage ? " \267 2/2" : " \267 1/2", sizeof s->label);   /* (the page: SELECT) */
        cu_cpy(s->footer, cs.key_play ? "SELECT: page \267 GLO: pick \267 OCT-: back"     /* (GLO: to the other) */
                                      : "SELECT: page \267 MAJ MIN: scale \267 GLO: play", sizeof s->footer);
        for (k = CU_PICK0; cs.key_on && k < CU_NKEY; k++)
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
        cu_picker(s, PERF_ITEMS, 7, cs.perf_sel, CR_COL_WHITE, cs.perform_on ? "perform \267 on" : "perform \267 off",
                  "SELECT: mode \267 ARP: on/off \267 OCT-: back");
        s->kind = CR_K_KNOBROW;
        s->orient = 1;
        if (cu_arp_layer(cu_now())) {              /* notes are being stepped and the picker's 2 s are over: the
                                                    * band is the arp (the chord's name, the hopping note); the mode's
                                                    * name moves into the label; the knob row as it is */
            const cr_snap_t *sn = &cr_snap;
            s->kr_band = 2;
            cu_name(&s->name, &sn->ci);
            cu_notes(s, &sn->ci);
            s->pos = (int8_t)(sn->perf_step && sn->perf_pos >= 0 && sn->perf_pos < (int)s->n_notes ? sn->perf_pos : -1);
            s->hop_col = CR_COL_WHITE;
            cu_cpy(s->label, PERF_ITEMS[cs.perf_sel % 7u], sizeof s->label);
            cu_cat(s->label, " \267 on", sizeof s->label);
        }
        cu_perf_cells(s);
        cu_row_trace(s, "perf", seen, sizeof seen);
        break;
    }
    case L_FX: {                                   /* the knob row: the effect over KNOB 1..4's cells */
        static char seen[48];                      /* (the trace: the row's labels when they change) */
        if (cs.fx_page == CU_FXPG_ROUTE) {         /* the Routing page: the signal flow (cr_draw.c cr_p_route) */
            uint32_t o = cu_rt_ord();
            cu_picker(s, FX_ITEMS, CU_NFX + 3u, CU_NFX + 2u, CR_COL_GREEN, "", "hold C# D# F# G# + KNOB 1 \267 A#: mode");
            s->kind = CR_K_ROUTE;
            cu_cpy(s->label, cs.fx_on ? (xfp[XP_RMODE] == 1 ? "fx \267 on \267 chain" : "fx \267 on \267 parallel")
                                      : (xfp[XP_RMODE] == 1 ? "fx \267 off \267 chain" : "fx \267 off \267 parallel"),
                   sizeof s->label);
            cu_cpy(s->title, "Routing", sizeof s->title);
            for (k = 0; k < XFE_N; k++)
                s->rt_order[k] = s->rt_from[k] = XF_ORD[o][k];
            s->rt_mode = (uint8_t)(xfp[XP_RMODE] == 1);
            s->rt_held = cu_rt.held;
            s->rt_fx_on = cs.fx_on;
            s->rt_filter_on = xfp[XP_FON] != 0;
            break;
        }
        cu_picker(s, FX_ITEMS, CU_NFX + 3u, cs.fx_page == CU_FXPG_SENDS ? 0u : cs.fx_page == CU_FXPG_FILTER ? CU_NFX + 1u
                                            : cs.fx_page == CU_FXPG_ROUTE ? CU_NFX + 2u
                                            : cs.fx_sel % CU_NFX + 1u, CR_COL_GREEN,
                  cs.fx_on ? "fx \267 on" : "fx \267 off", "SELECT: effect \267 FX: on/off \267 OCT-: back");
        if (CU_FXP[cu_fx_pg()].of > 1u)            /* the page, quietly (as the KEY layer's): SELECT steps through both */
            cu_cat(s->label, cs.fx_page ? " \267 2/2" : " \267 1/2", sizeof s->label);
        s->kind = CR_K_KNOBROW;
        s->orient = 1;
        cu_fx_cells(s);
        cu_row_trace(s, "fx", seen, sizeof seen);
        break;
    }
    case L_LOCK: {                                 /* B3 held: LOCK's two styles, a knob row under its name */
        static const char *const IT[1] = {"Lock"};
        static char seen[48];
        cu_picker(s, IT, 1, 0, CR_COL_WHITE, cu.clock ? "lock \267 on" : "lock \267 off",
                  cu_fm1() ? "LFO: on/off \267 OCT-: back \267 HOME: home" : "B: on/off \267 OCT-: back \267 HOME: home");
        s->kind = CR_K_KNOBROW;
        s->orient = 1;
        s->cell[0][0].flags = s->cell[0][1].flags = CR_CF_ON;
        cu_cpy(s->cell[0][0].label, "Type key", sizeof s->cell[0][0].label);
        cu_cpy(s->cell[0][0].value, cs.lock_keep ? "Keeps" : "Clears", sizeof s->cell[0][0].value);
        cu_cpy(s->cell[0][1].label, "Ext keys", sizeof s->cell[0][1].label);
        cu_cpy(s->cell[0][1].value, cs.lock_push ? "Pushed" : "Toggle", sizeof s->cell[0][1].value);
        cu_hot_row(s, L_LOCK);
        cu_row_trace(s, "lock", seen, sizeof seen);
        break;
    }
    case L_VIEW: {                                 /* HOME held: the views, the lock as the last line */
        const char *it[V_N + 1];
        for (k = 0; k < V_N; k++)
            it[k] = CU_VIEW[k];
        it[V_N] = cs.view_lock ? "Lock: On" : "Lock: Off";
        cu_picker(s, it, V_N + 1u, cs.view % V_N, CR_COL_WHITE, cs.view_lock ? "view \267 locked" : "view",
                  "SELECT: view \267 KNOB 1: lock \267 OCT+: ok");
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
                  "SELECT: engine \267 OCT-: cancel \267 OCT+: ok");
        cu_cpy(s->value, psnd[ce.part].name, sizeof s->value);
        if (psnd[ce.part].edited)
            cu_cat(s->value, "*", sizeof s->value);
        s->star = (uint8_t)cu_fav_part(ce.part);   /* a favourite (KNOB 4 here): the star before its name */
        break;
    case L_LOOP:                                   /* the PLAY menu (no ring: a loop running shows the dial) */
        cu_loopmenu(s, cu_now());
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
    if (cu.opt_sel != O_RESET)
        cu_reset_ask = 0;
    {                                              /* the entries listed (cu_opt_vis), the selected one's place */
        const char *it[O_N];
        uint32_t o, n = 0, at = 0;
        for (o = 0; o < cu_opt_n(); o++)
            if (cu_opt_vis(o)) {
                if (o == cu.opt_sel)
                    at = n;
                it[n++] = O_NAME[o];
            }
        cu_picker(s, it, n, at, CR_COL_WHITE, "options \267 KNOB 1 sets", "SELECT: move \267 OCT-: back \267 HOME");
    }
    opt_text(cu.opt_sel, s->value, sizeof s->value);
    if (cu.opt_sel == O_SAFE)                      /* "Safe mode: flash data skipped \267 OCT- OCT+ HOME 5 s: update" (0.18) */
        cu_cpy(s->label, "OCT- OCT+ HOME 5 s: update", sizeof s->label);
    else if (cu.opt_sel == O_ERASE)                /* "Flash data: erase and reboot", OCT+ twice */
        cu_cpy(s->label, cu_erase_ask ? "sounds, loops, settings go" : "OCT+ twice: erase, reboot", sizeof s->label);
    else if (cu.opt_sel == O_RESET)                /* "Reset Settings: all options to default", OCT+ twice */
        cu_cpy(s->label, cu_reset_ask ? "sounds, loops, calibration stay" : "OCT+ twice: reset", sizeof s->label);
    else if (cu.opt_sel == O_USB_IN)               /* what each USB entry does, under its value */
        cu_cpy(s->label, "records master, chord, bass", sizeof s->label);
    else if (cu.opt_sel == O_USB_LEVEL)            /* (the values stay Master / Fixed: the line says what they do) */
        cu_cpy(s->label, cs.usb_fixed ? "record full, MASTER: speaker" : "record level: MASTER knob", sizeof s->label);
    else if (cu.opt_sel == O_USB_RATE)             /* (48 kHz is the computer's choice once offered; it costs the
                                                    * audio 6 % while it records: docs/USB-AUDIO.md "48 kHz") */
        cu_cpy(s->label, cs.usb48 ? "the computer picks the rate" : "48 kHz: not offered", sizeof s->label);
    else if (cu.opt_sel == O_LAYOUT)               /* (docs/CHORD-LAYOUT.md) */
        cu_cpy(s->label, cs.layout ? "SUS: G#3 + A#3 \267 LOCK: LFO" : "SUS: C#4 \267 LOCK: B3", sizeof s->label);
    else if (cu.opt_sel == O_SUSWIN)
        cu_cpy(s->label, "G#3 A#3 together within it", sizeof s->label);
    else if (cu.opt_sel == O_FULLSHORT)
        cu_cpy(s->label, "Full: F#5 chord keys \267 G5 chords", sizeof s->label);
    else if (cu.opt_sel == O_MIDI_KEYS)
        cu_cpy(s->label, "notes 53..79 press F3..G5", sizeof s->label);
    else if (cu.opt_sel == O_SIXTH)                /* (docs/CHORD-NAMES.md "The 6th above a seventh": C7 + 6 and the like) */
        cu_cpy(s->label, cs.sixth_up ? "6 + 7: the 6th on top, a 13th" : "6 + 7: the 6th under the 7th", sizeof s->label);
#if BOARD_SMK37
    else if (cu.opt_sel == O_PEDAL)                /* (SMK37.md "The pedal jack") */
        cu_cpy(s->label, cs.smk_pedal ? "the jack: CC 11, both parts" : "the jack: CC 64, both parts", sizeof s->label);
#endif
    else if (cu.opt_sel == O_NAMES)                /* (docs/CHORD-NAMES.md: the same notes under either name) */
        cu_cpy(s->label, cs.names_std ? "A9 \267 Aadd9 \267 A9sus4" : "A7 9 \267 A9 \267 Asus m7 9", sizeof s->label);
    else if (cu.opt_sel == O_VERSION) {            /* the unit's ID under the version (3y66: several FM-1s on one
                                                    * computer, fm1_install.py --unit) */
        cu_cpy(s->label, cr_unit_id[0] ? "unit ID " : "no unit ID", sizeof s->label);
        cu_cat(s->label, cr_unit_id, sizeof s->label);
    }
    else if (cu.opt_sel == O_OUTPUT)               /* (docs/AUDIO-QUALITY.md: 0.19's output chain against 0.18's) */
        cu_cpy(s->label, cs.out_clean ? "0.19: linear, MASTER last" : "0.18: limiter, soft clip", sizeof s->label);
    else if (cu.opt_sel == O_DUALCORE && !CR_SINGLE_CORE && !cs.dualcore && cr_c1_barred) {   /* (cr_cpu1.h: kept
                                                    * off after a crash or a hang beside core 1, or given up this
                                                    * session) */
        static const char *const BAR[4] = {"", "off after a crash on 2 cores", "off after a hang on 2 cores",
                                           "off: the 2nd core gave up"};
        cu_cpy(s->label, BAR[cr_c1_barred & 3u], sizeof s->label);
    } else if (cu.opt_sel == O_DUALCORE)           /* (docs/DUALCORE.md: On the default since 0.22; never started /
                                                    * stopped live) */
        cu_cpy(s->label, CR_SINGLE_CORE ? "the single core package"
                         : cs.dualcore != !cr_c1_off ? "takes effect after a restart"
                         : cs.dualcore ? "bass on the second core" : "On: bass on the second core", sizeof s->label);
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
    if (cu.epk_on == 2u)                           /* the bass part's: orange */
        cu_picker(s, eng, n, cu.epk_rank, CR_COL_ORANGE, "bass engine \267 OPT + ALGORITHM", "");
    else
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
    char b[32];
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
    if (!ce.save_step) {                           /* the menu: five entries (P5: the last three). The two saves by
                                                    * their names; the other three show their VALUE big (the entry not
                                                    * selected: a line that names it), the line under the name says
                                                    * which entry it is */
        static const char *const TYSM[3] = {"Type: Both", "Type: Chord", "Type: Bass"};
        uint32_t ch = ce.save_choice, tag = 0, id = 0, ty;
        int fav = cu_fav_part(part), only = cu_save_favonly(part);
        const char *it[CU_SAVE_N];
        ty = cu_fav_part_key(part, &tag, &id) ? crf_type(tag, id) : CRF_BOTH;   /* (the sound's, marked or not) */
        it[0] = CHOICE[0];
        it[1] = CHOICE[1];
        it[CU_SAVE_FAV] = fav ? "Favorite" : "Not a favorite";
        it[CU_SAVE_FILT] = crf_filter ? "Filter on" : "Filter off";
        it[CU_SAVE_TYPE] = ch != CU_SAVE_TYPE ? TYSM[ty] : CU_FAV_TYPE[ty];
        if (ch == CU_SAVE_FILT) {
            cu_cpy(b, crf_filter && !cuf_n(part, e) ? "no favorites here: all" : "favorites filter", sizeof b);
        } else if (ch == CU_SAVE_TYPE) {             /* (not a favourite: said here, and the name has no star) */
            cu_cpy(b, fav ? "favorite type" : "type \267 not a favorite", sizeof b);
        } else {
            cu_cpy(b, ch == CU_SAVE_FAV ? (part ? "bass \267 " : "") : part ? "save bass \267 " : "save \267 ", sizeof b);
            cu_cat(b, ENGINES[e]->name, sizeof b);
            if (ch == CU_SAVE_FAV)
                cu_cat(b, " \267 favorite", sizeof b);
        }
        cu_picker(s, only ? it + CU_SAVE_FAV : it, only ? CU_SAVE_N - CU_SAVE_FAV : CU_SAVE_N,
                  only ? ch - CU_SAVE_FAV : ch, col, b,
                  ch >= CU_SAVE_FAV ? CU_SAVE_FOOT : "OCT+: save \267 OCT-: cancel");
        cu_cpy(s->value, psnd[part].name, sizeof s->value);
        if (psnd[part].edited)
            cu_cat(s->value, " *", sizeof s->value);
        s->star = (uint8_t)fav;
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
    s->lines[1].cur = (uint8_t)(cn.len ? cn.cur + 1u : 0u);   /* the cursor: under its character (no name: none) */
    s->lines[2].px = 12;
    cu_cpy(s->lines[3].t, "SELECT: cursor \267 " CU_K1 ": letter", sizeof s->lines[3].t);
    s->lines[3].px = 12;
    s->lines[3].col = CR_COL_GREY;
    cu_cpy(s->lines[4].t, "keys: letters \267 " CU_K4 " left: delete", sizeof s->lines[4].t);
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

/* the Chord view's content while an arpeggio plays: the arp screen (the chord's name, its notes on a line, the
 * sounding note hopping along). Only there (the other views are never replaced) and only while the shown chord is
 * being stepped: Perform on in a clocked mode (Arpeggiate, Arp 2 Octaves, Pattern: cr_snap.perf_step; a strum, a slop
 * and a harp run once and keep the chord view) from the press to the voice's end (keys up without LATCH / Hold,
 * Perform off, PANIC, the loop that played it stopped). Options > Motion does not gate it (it did until 2026-10-10:
 * the owner's unit had Motion Off and showed no arp screen): the hop is the arpeggio's state, redrawn a step at a
 * time, not a tween; Motion Off only drops the stripes' sweep before it (cu_animate). A chord re-fingered within
 * CU_ARP_LINGER_MS keeps the screen (no flash of the chord view between two chords: the name and the notes of the one
 * just left, no note on its block); Perform off does not wait, and the PANIC screen outlasts it */
#define CU_ARP_LINGER_MS 150u
typedef struct { uint32_t t_step; uint8_t seen; } cu_arp_t;   /* a place that shows the arp: its linger */
static int cu_arp_run(cu_arp_t *a, uint32_t now, int here)    /* here: the place may show it now */
{
    const cr_snap_t *sn = &cr_snap;
    uint32_t pm = sn->perform_mode;
    if (!here || !sn->perform_on || !sn->ci.valid || (pm != CR_PM_ARP && pm != CR_PM_PATTERN)) {
        a->seen = 0;
        return 0;
    }
    if (sn->perf_step && sn->ci.sounding) {
        a->seen = 1;
        a->t_step = now;
        return 1;
    }
    if (a->seen && now - a->t_step < CU_ARP_LINGER_MS)
        return 1;
    a->seen = 0;
    return 0;
}
static int cu_arp_on(uint32_t now)                 /* the Chord view */
{
    static cu_arp_t a;
    return cu_arp_run(&a, now, cs.view == V_CHORD);
}
/* the PERF layer's band (the owner, 2026-10-10): the same rule, whatever the view, but the picker comes first: for
 * CU_PERF_PICK_MS after the layer opened and after every detent of SELECT the band is the mode picker (so a mode
 * switched under a latched arp is read), then the arp while notes are stepped; not stepping: the picker, as before.
 * KNOB 1..4 do not bring the picker back (their cells turn hot) */
#define CU_PERF_PICK_MS 2000u
static int cu_arp_layer(uint32_t now)
{
    static cu_arp_t a;
    return cu_arp_run(&a, now, now - cu.perf_t0 >= CU_PERF_PICK_MS);
}

/* GEEK OUT 2 (the owner, 2026-10-14): four meters, the oscillator mixer's tall bars as a read-out (cr_draw.c cr_p_tall,
 * tall 2) under the editor's title line with the battery: CORE 0, the audio ISR's share of a half over the last second
 * (audio.c cpu_window: its render, less its waits for core 1); CORE 1, core 1's busy time over the same second (cr_cpu1.h
 * c1_avg_us / c1_max_us: the bass part's job with its share of P5's chord voices, and the reverb, 3y46 / 3y50; not
 * running: dim, cr_c1_name); RAM and POOL in use (app.ld, link-time: build.sh's size line). The core
 * bars glide a quarter of the way to the second's figure each frame (Motion Off: at once; from 0 on coming to the
 * view), the second's peak a tick. The emulator has no link map, and its ISR's wall time is the host's: its own
 * CR_MEM_USED and CR_CORE0_US (emu_firmware.h) */
#ifndef CR_CORE0_US
#define CR_CORE0_US(avg, max) ((avg) = cpu_last.avg_us, (max) = cpu_last.max_us)
#endif
#ifndef CR_MEM_USED
extern uint32_t _bss_end[], _pool_start[], _pool_end[];          /* app.ld (main.c's) */
#define CR_MEM_USED(r, p) ((r) = (uint32_t)(uintptr_t)_bss_end - 0x01C08000u, \
                           (p) = (uint32_t)(uintptr_t)_pool_end - (uint32_t)(uintptr_t)_pool_start)   /* (RAM's origin) */
#endif
#define CU_RAM_BYTES 98304u                       /* app.ld: RAM 96 KiB, POOL 0x54000 */
#define CU_POOL_BYTES 0x54000u
static void cu_g2_cell(cr_cell_t *c, const char *label, uint32_t pct, uint32_t peak, uint32_t v100)
{
    char b[8];
    c->flags = CR_CF_ON | CR_CF_PCT;
    c->glyph = CR_G_BAR;
    cu_cpy(c->label, label, sizeof c->label);
    c->pct = (uint8_t)(pct > 255u ? 255u : pct);
    c->pct2 = (uint8_t)(peak > 255u ? 255u : peak);
    cu_int(b, (int32_t)v100, 0, sizeof b);
    cu_cat(b, "%", sizeof b);
    cu_cpy(c->value, b, sizeof c->value);
}
static void cu_geek2(cr_screen_t *s, uint32_t now)
{
    static uint32_t shown[2], seen;              /* the core bars as shown (Q12 of a half), the last frame here */
    uint32_t us[2], pk[2], k, ram, pool, dual = 0;
    CR_CORE0_US(us[0], pk[0]);
    us[1] = pk[1] = 0;
#if CR_CPU1
    if (cr_dbg.c1 == C1_OK) {
        dual = 1;
        us[0] = us[0] > cr_dbg.avg_us[CRP_WAIT] ? us[0] - cr_dbg.avg_us[CRP_WAIT] : 0u;
        pk[0] = pk[0] > cr_dbg.max_us[CRP_WAIT] ? pk[0] - cr_dbg.max_us[CRP_WAIT] : 0u;
        us[1] = cr_dbg.c1_avg_us;                /* (its voices and the reverb: not CRP_PART1 alone, the voices') */
        pk[1] = cr_dbg.c1_max_us;
    }
#endif
    if (now - seen > 200u)                       /* (back on the view: the bars rise from 0) */
        shown[0] = shown[1] = 0;
    seen = now;
    for (k = 0; k < 2u; k++) {
        uint32_t t = us[k] >= CPU_HALF_US ? 4096u : us[k] * 4096u / CPU_HALF_US;
        int32_t d = (int32_t)t - (int32_t)shown[k];
        shown[k] = cr_motion == CR_MOTION_OFF || (d > -8 && d < 8) ? t : (uint32_t)((int32_t)shown[k] + d / 4);
    }
    s->kind = CR_K_EDIT8;
    s->header = 0;                               /* (its own title line, as the editor's) */
    s->ring_on = 0;
    s->tall = 2;
    s->n_rows = 1;
    s->active = 0;
    s->batt = (uint8_t)cu_batt();
    cu_cpy(s->title, "GEEK OUT 2", sizeof s->title);
    s->title_col = CR_COL_WHITE;
    if (cs.touch) {                              /* the keys sensing: the last key's velocity, the aftertouch now */
        char b[8];
        cu_cpy(s->page, "VEL ", sizeof s->page);
        cu_int(b, cu.last_vel, 0, sizeof b);
        cu_cat(s->page, b, sizeof s->page);
        cu_cat(s->page, " AT ", sizeof s->page);
        cu_int(b, cu.last_at, 0, sizeof b);
        cu_cat(s->page, b, sizeof s->page);
    } else
        cu_cpy(s->page, dual ? "2 cores" : "1 core", sizeof s->page);
    cu_g2_cell(&s->cell[0][0], "CORE 0", shown[0] >> 4, pk[0] * 256u / CPU_HALF_US,
               (us[0] * 100u + CPU_HALF_US / 2u) / CPU_HALF_US);
    if (dual)
        cu_g2_cell(&s->cell[0][1], "CORE 1", shown[1] >> 4, pk[1] * 256u / CPU_HALF_US,
                   (us[1] * 100u + CPU_HALF_US / 2u) / CPU_HALF_US);
    else {                                       /* one core: core 1's word, in the grey */
        cu_g2_cell(&s->cell[0][1], "CORE 1", 0, 0, 0);
        s->cell[0][1].flags |= CR_CF_DIM;
#if CR_CPU1
        cu_cpy(s->cell[0][1].value, cr_c1_name(), sizeof s->cell[0][1].value);
#else
        cu_cpy(s->cell[0][1].value, "off", sizeof s->cell[0][1].value);
#endif
    }
    CR_MEM_USED(ram, pool);
    if (ram == 0xFFFFFFFFu) {                    /* (no link map: the emulator) */
        for (k = 2; k < 4u; k++) {
            cu_g2_cell(&s->cell[0][k], k == 2u ? "RAM" : "POOL", 0, 0, 0);
            s->cell[0][k].flags |= CR_CF_DIM;
            cu_cpy(s->cell[0][k].value, "n/a", sizeof s->cell[0][k].value);
        }
        return;
    }
    cu_g2_cell(&s->cell[0][2], "RAM", ram * 256u / CU_RAM_BYTES, 0, (ram * 100u + CU_RAM_BYTES / 2u) / CU_RAM_BYTES);
    cu_g2_cell(&s->cell[0][3], "POOL", pool * 256u / CU_POOL_BYTES, 0,
               (pool * 100u + CU_POOL_BYTES / 2u) / CU_POOL_BYTES);
}

static void cu_view_screen(cr_screen_t *s, uint32_t now)
{
    const cr_snap_t *sn = &cr_snap;
    const cr_chord_info_t *ci = &sn->ci;
    if (cu_arp_on(now)) {
        uint32_t pm = sn->perform_mode;
        char v[8], sub[12];
        int32_t p = CU_PERF_KNOB[pm][0];
        s->kind = CR_K_ARP;                        /* the arp in motion: the sounding note hops along */
        cu_name(&s->name, ci);
        cu_notes(s, ci);
        cu_numeral_put(s);
        s->pos = (int8_t)(sn->perf_step && sn->perf_pos >= 0 && sn->perf_pos < (int)s->n_notes ? sn->perf_pos : -1);
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
    case V_GEEK2:                                 /* four meters: the two cores, RAM, POOL */
        cu_geek2(s, now);
        break;
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
            if (cr_dbg.c1 == C1_OFF && cr_c1_barred) {         /* kept off: why ("c1 off \267 after a crash") */
                cu_cat(s->lines[3].t, " \267 ", sizeof s->lines[3].t);
                cu_cat(s->lines[3].t, cr_c1_bar_name(), sizeof s->lines[3].t);
            }
            if (cr_dbg.c1 == C1_GAVE_UP)                       /* the way out: the switch (docs/DUALCORE.md) */
                cu_cat(s->lines[3].t, " \267 Options>Dual Core", sizeof s->lines[3].t);   /* (30 chars of the 31 a line holds) */
            else if (cr_dbg.c1 == C1_OK) {                     /* misreads core 1 refused (docs/DUALCORE.md) */
                cu_cat(s->lines[3].t, " \267 rej ", sizeof s->lines[3].t);
                cu_int(b, (int32_t)cr_c1_rejected(), 0, sizeof b);
                cu_cat(s->lines[3].t, b, sizeof s->lines[3].t);
                if (cr_c1_faults()) {
                    cu_cat(s->lines[3].t, " \267 flt ", sizeof s->lines[3].t);
                    cu_int(b, (int32_t)cr_c1_faults(), 0, sizeof b);
                    cu_cat(s->lines[3].t, b, sizeof s->lines[3].t);
                }
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
#if FELUCCA_UAC
        {   /* ChoralRoot In (usb_audio_stream.c): "usb 12345 u0 o0 f256": packets sent, ring under/over-runs, its fill */
            char b[12];
            char *t = s->lines[s->n_lines].t;
            cu_cpy(t, "usb ", sizeof s->lines[0].t);
            cu_int(b, (int32_t)ua.tx_packets, 0, sizeof b);
            cu_cat(t, b, sizeof s->lines[0].t);
            cu_cat(t, " u", sizeof s->lines[0].t);
            cu_int(b, (int32_t)ua.cap_underruns, 0, sizeof b);
            cu_cat(t, b, sizeof s->lines[0].t);
            cu_cat(t, " o", sizeof s->lines[0].t);
            cu_int(b, (int32_t)ua.cap_overruns, 0, sizeof b);
            cu_cat(t, b, sizeof s->lines[0].t);
            cu_cat(t, " f", sizeof s->lines[0].t);
            cu_int(b, (int32_t)(ua.cw - ua.cr), 0, sizeof b);
            cu_cat(t, b, sizeof s->lines[0].t);
            s->n_lines++;
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
        cu_numeral_put(s);
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
    if (sn->ci.sounding || (cu.kheld >> cu_root0()))
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
                if (!l && cu.page != PG_EDIT && cu.page != PG_SAVE && !cu.opt_open && cs.view == V_CHORD)
                    cu_numeral_put(s);             /* (the Chord view behind it: its numeral stays) */
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
            s->star = cu.pop.star;
            s->label_fav = cu.pop.filt;
            if (cu.pop.over[0]) {                  /* a pool's read-out on the home pages: the engine over the number */
                cu_cpy(s->line, cu.pop.over, sizeof s->line);
                s->line_col = CR_COL_ORANGE;
            }
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
    cu_erase_ask = cu_reset_ask = 0;               /* (the Options left with an "OCT+ again" pending: it starts again;
                                                    * until 2026-10-12 Flash Data's stayed armed behind a closed menu) */
    /* 6. the stripes: nothing touched since power-on (the splash stays; a PANIC's cleared chord no longer brings
     * them back), or the screensaver: no input (a key, a button,
     * a knob, a MIDI message in) for CR_IDLE_MS, 3 minutes; a loop playing alone does not keep the screen awake, and any
     * input brings the view back at once (docs/INTEGRATION.md, the splash and the screensaver) */
    if ((!sn->ci.valid && cu.last_input == cu_boot_ms) || now - cu.last_input >= CR_IDLE_MS) {
        cu_stripes(s);
        cu_dial(s, now);
        return;
    }
    /* 5. the View */
    cu_view_screen(s, now);
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
    uint8_t segs;                                 /* meter: its stripes */
    uint8_t pool;                                 /* meter: the pool it read out last frame (cu.pop.pool; 0: none) */
    uint8_t pool_of[2];                           /* .. per part (chord, bass): the pool its read-out last showed */
    uint16_t pool_pct[2];                         /* .. and the fill it showed (its place) */
} ca;
static cr_anim_t cu_anim;

static int cu_name_eq(const cr_name_t *a, const cr_name_t *b)
{
    return cu_eq(a->root, b->root) && cu_eq(a->quality, b->quality) && cu_eq(a->sup, b->sup);
}

static void cu_animate(cr_screen_t *s, uint32_t now)
{
    int kind_changed = s->kind != ca.kind;
    uint32_t pool = 0;                             /* the pool whose read-out this frame's meter is (0: none) */
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
        if (kind_changed && ca.kind != CR_K_ARP) {
            ca.from = NONAME;
            ca.anim = CR_A_SQUEEZE;
            cr_anim_mark(&cu_anim, now);
        } else if (!cu_name_eq(&s->name, &ca.name)) {
            ca.from = ca.name;
            ca.anim = CR_A_SQUEEZE;
            cr_anim_mark(&cu_anim, now);
        } else if (kind_changed) {                 /* the arp stopped: its chord, as it stands (no squeeze) */
            ca.from = NONAME;
            ca.anim = 0;
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
    case CR_K_ROUTE: {                             /* the Routing page: a move slides the blocks, a hold lifts, a
                                                    * release lands (cr_draw.c cr_p_route) */
        static uint8_t ord[4], from[4], held, drop;
        uint32_t i, moved = 0;
        for (i = 0; i < 4u; i++)
            moved |= ord[i] != s->rt_order[i];
        if (kind_changed || cr_motion == CR_MOTION_OFF) {
            ca.anim = 0;
            drop = 0;
            for (i = 0; i < 4u; i++)
                from[i] = s->rt_order[i];
            cr_anim_mark(&cu_anim, now);
        } else if (moved) {
            for (i = 0; i < 4u; i++)
                from[i] = ord[i];
            ca.anim = CR_A_SLIDE;
            drop = 0;
            cr_anim_mark(&cu_anim, now);
        } else if (s->rt_held != held) {
            drop = s->rt_held ? 0 : held;
            ca.anim = CR_A_LIFT;
            cr_anim_mark(&cu_anim, now);
        }
        for (i = 0; i < 4u; i++) {
            ord[i] = s->rt_order[i];
            s->rt_from[i] = from[i];
        }
        held = s->rt_held;
        s->rt_drop = drop;
        s->slide = 1;
        s->anim = ca.anim;
        break;
    }
    case CR_K_METER:
        pool = cu.pop.kind == PU_METER && (int32_t)(cu.pop.until - now) > 0 ? cu.pop.pool : 0u;
        if (pool) {                                /* a pool's read-out (cu_pool_popup): its label carries the place,
                                                    * so the pool tells whether it is the same meter. The same part
                                                    * and engine as that part's read-out last showed: the stripes
                                                    * move from where they stand (still on their way: from there; the
                                                    * read-out shown again: from the place it last showed) to the new
                                                    * place, up or down. Another engine, or the part's first read-out
                                                    * since power-on: they fill from none */
            uint32_t pi = (pool - 1u) & 1u;
            if (kind_changed || pool != ca.pool || s->pct != ca.pct || !cu_eq(s->value, ca.value)) {
                if (ca.pool_of[pi] != pool)
                    ca.pct_from = 0;
                else if (!kind_changed && pool == ca.pool && ca.segs) {
                    uint32_t a = ((uint32_t)ca.pct_from * ca.segs + 128u) >> 8;
                    uint32_t b = ((uint32_t)ca.pct * ca.segs + 128u) >> 8, f = b, to = cr_filled(s, s->pct);
                    if (ca.anim & CR_A_FILL)
                        f = cr_fill_at(a, b, cr_anim_ms(&cu_anim, now));
                    if (f != b && ca.segs == s->segments)   /* still on their way: this step moves one at once, so
                                                             * a knob turned faster than a stripe every CR_FILL_MS
                                                             * (every step restarts the fill) is still followed */
                        f = to > f ? f + 1u : to < f ? f - 1u : f;
                    ca.pct_from = (uint16_t)(f * 256u / ca.segs);
                } else
                    ca.pct_from = ca.pool_pct[pi];
                ca.anim = CR_A_FILL;
                cr_anim_mark(&cu_anim, now);
            }
            ca.pool_of[pi] = (uint8_t)pool;
            ca.pool_pct[pi] = s->pct;
        } else if (kind_changed || ca.pool || !cu_eq(s->label, ca.label)) {
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
        ca.segs = s->segments;
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
    ca.pool = (uint8_t)pool;
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
    static uint32_t bpm_n, msg_n;
    crm_act_t a;
    if (msg_n != cr_in_msg_n) {                   /* a channel message in: activity (the screensaver) */
        msg_n = cr_in_msg_n;
        cu.last_input = cu_now();
    }
    if (bpm_n != cr_in_bpm_n) {
        bpm_n = cr_in_bpm_n;
        cs.bpm = cr_in_bpm;                       /* (the ISR set the engine's: no CRE_TEMPO back) */
        song.g[G_BPM] = (int16_t)(cs.bpm < 40u ? 40u : cs.bpm > 240u ? 240u : cs.bpm);
        xf_bpm = cs.bpm;                          /* (the synced delay follows the clock: its times glide) */
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
            cu_send_set(a.part ? 1u : 0u, id, a.v);   /* (part 0's sends are the FX amounts: cu_send_set) */
            cu_send_popup(a.v, i < CU_NFX ? CU_FX[i].name : "send", p ? CR_COL_ORANGE : CR_COL_GREEN);
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
    {   /* core 1 kept off at boot (cr_cpu1.h cr_c1_bar_boot): told once, after the splash; given up this session
         * (cr_c1_give_up): Dual Core Off, saved (a restart keeps one core either way: the .noinit bar) */
        static uint8_t cu_c1_told, cu_c1_seen;
        static const char *const MSG[4] = {"", "dual core off: crash", "dual core off: hang", "dual core off: gave up"};
        if (cr_dbg.c1_fallback && !cu_c1_seen && !CR_C1_INJECTED()) {   /* (an injected failure, a CR_CORE1_TEST
                                                    * build's 75 CORE1_TEST, is not the unit's: nothing saved) */
            cu_c1_seen = cu_c1_told = 1;
            cr_c1_barred = C1_BAR_GAVE_UP;
#if CR_HAVE_SETTINGS
            cr_settings_dc_off();
#endif
            cu_message(MSG[C1_BAR_GAVE_UP], CR_COL_RED);
        } else if (cr_c1_barred && !cu_c1_told && cu_now() - cu_boot_ms >= CR_SPLASH_MS) {
            cu_c1_told = 1;
            cu_message(MSG[cr_c1_barred & 3u], CR_COL_RED);
        }
    }
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

/* SysEx 74 SCREEN (cr_backup.c): 0 the screen off (black, as the idle timer's) until SysEx 1 or a key, button or
 * knob (MIDI in does not wake it: a test driving the unit over MIDI keeps it black); 1 on (as an input: the idle
 * clocks restart) */
static void cu_screen_sysex(int on)
{
    cu.sx_off = (uint8_t)!on;
    cu.sx_local = cu.last_local;
    if (on)
        cu.last_input = cu_now();
    cu_trace("screen: sysex %s\n", on ? "on" : "off");
}

static void cr_ui_draw(void)
{
    uint32_t now = cu_now();
    int off;
    if (cu.sx_off && cu.last_local != cu.sx_local)  /* a key, button or knob: the SysEx off ends */
        cu.sx_off = 0;
    off = cu.sx_off || now - cu.last_input >= CR_SCREEN_OFF_MS;
    cr_build_screen(&cu_scr, now);
    if (off) {                                    /* the screen off: the model ticks, nothing is drawn */
        if (!cu.screen_off) {
            cu.screen_off = 1;
            cr_draw_black();                      /* one black frame. The backlight stays on: its pin (PA2) also
                                                   * keeps the keys, buttons, knobs and LEDs alive, and they are
                                                   * what wakes the screen (hal/fm1_lcd_hw.h) */
            cu_trace("screen: off (%s)\n", cu.sx_off ? "sysex" : "idle");
        }
        cu_animate(&cu_scr, now);
        return;
    }
    if (cu.screen_off) {                          /* woken: the whole view at once (no sweep off the black logo) */
        if (ca.kind == CR_K_STRIPES)
            ca.kind = CR_K_NONE;
        cr_draw_invalidate();                     /* (every strip, whole, over the black frame) */
    }
    cu_animate(&cu_scr, now);
    cr_draw_idle = cr_ui_pump;                    /* the keys are read between its strips (cr_ui_pump) */
    cr_draw(&cu_scr, cr_anim_ms(&cu_anim, now));
    cr_draw_idle = 0;
    if (cu.screen_off) {                          /* drawn */
        cu.screen_off = 0;
        cu_trace("screen: on\n");
    }
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
    palette_set(NPALETTES - 1u);                  /* MOD: ChoralRoot's (gfx.c palettes: the last) */
    fm6_init();                                   /* every part's FM6 patch: the init voice */
    for (k = 0; k < G_COUNT; k++)
        song.g[k] = GP[k].def;
    xf_defaults();                                /* cr_fx.c: the delay's and the reverb's (the session's over them) */
    for (k = 0; k < NPART; k++)
        for (m = 0; m < P_E0; m++)
            trk[k].p[m] = TP[m].def;
    song.master_q12 = 2048;
    cr_out_init();
    cr_bank_boot();                               /* (the device: persist_boot did it, with the flash) */
    cb_init();
    cu_led_init();
#if BOARD_SMK37
    smk_pad_map = CU_PAD_NOTE;                    /* the pads: the chord block's keys, all the UI's (cu_smk_pad) */
#endif
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
    cs.suswin = 30;                               /* (the settings' default, before a record is read) */
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
    cs.fx_page = CU_FXPG_SENDS;                   /* the FX layer opens on its first page, the Sends */
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
    cu_sends_default();                           /* (both parts' sends: the session's over them) */
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
    cs.loop_rate = CRL_RATE_1;
    cs.metro_vol = 70;
    cs.dualcore = 1;                              /* Options > Dual Core On (the record's: cr_settings_load) */
    cs.out_clean = 1;                             /* Options > Output Clean (the record's) */
    cs.usb48 = 0;                                 /* Options > USB Rate 44.1 kHz (the record's) */
    cs.names_std = 0;                             /* Options > Chord Names Orchid (the record's) */
    cs.sixth_up = 0;                              /* Options > 6th with 7th In place (the record's) */
    cr_chord_names_set(0);
#if !CR_HAVE_SETTINGS
    fx_output_set(1, 1);                          /* (a build without the record: nothing else tells fx.c) */
#endif
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
    cu.last_input = cu.last_local = cu_boot_ms;
    cr_anim_mark(&cu_anim, cu_boot_ms);
    cr_draw_invalidate();
}
