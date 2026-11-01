/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the sounds (docs/PRESETS.md, docs/INTEGRATION.md section 7).
 *
 *   1. what upreset.c (Felucca's 32 user slots, kept unedited) calls of Felucca's dropped UI (ui.c), with
 *      ChoralRoot's meaning: no undo copy (load_begin / load_end), no pattern (load_pat16 / load_grid16), the
 *      messages through cr_ui.c (ui_message, ui_say: prototypes here, bodies in cr_ui.c), `ui` (main.c's fields);
 *   2. upreset.c itself (and fm6_bank.c, which it includes);
 *   3. the trims of the loud factory presets, by (engine, preset name);
 *   4. the pools PRESETS / ALGORITHM turn through: one per engine, INIT, the factory presets (each replaced by the
 *      user's record bound to it), the user's added presets; the save into a slot with its binding (cb_store).
 * Included before cr_ui.c, after storage.c (device) and cr_anim.c. */

/* ------------------------------------------------ 1. upreset.c's UI names --- */
static struct {
    uint8_t home, force, uboot, page;   /* main.c: breadcrumbs, a full redraw, the UPDATE MODE countdown */
    uint8_t ppick;                      /* (upreset.c: SEQ > PATTERNS' pick; nothing reads it here) */
} ui;
static void ui_message(const char *s);            /* cr_ui.c: a message box */
static void ui_say(const char *a, const char *b);
static uint32_t up_gen;                           /* bumped on every user bank change */
/* 3y60: the pools' order (section 4), resolved once from upool.c's name table and kept until a slot changes (upn_note
 * clears ok; up_gen moving does too): each slot's pool and the factory place it is bound to (PF_NONE: added, or not
 * used), the added sounds per engine. Browsing never reads a record from flash (0d227e0 read up to 5500 a detent) */
static struct {
    uint32_t gen;
    uint8_t ok, eng[UP_SLOTS], bnd[UP_SLOTS], nadd[NENGINES];
} cbc;
static uint8_t sync_reload;                       /* (Felucca's editor RELOAD push: no editor) */
enum { UNDO_SOUND = 1, UNDO_PAT = 2 };
static void load_begin(track_t *t, uint32_t what) { (void)t; (void)what; }   /* ChoralRoot keeps no undo copy */
static void load_end(track_t *t) { (void)t; }
#if FELUCCA_SEQ
static int chain_busy(void) { return chain.running || chain.armed; }
#else
static int chain_busy(void) { return 0; }        /* (no song chain: FELUCCA_SEQ 0) */
#endif
static int transport_busy(void)                   /* no flash erase while Felucca's transport runs (inert here) */
{
    int busy;
    fm1_irq_off();
    busy = song.playing || chain_busy() || transport_req == 1u;
    fm1_irq_on();
    return busy;
}
/* ui.c param_kept: the part's own parameters, which a sound load leaves (the mix, the arp, the sequencer) and, since
 * 0.18 (the owner, 2026-10-12: "I don't think we should save the sends with the presets ... I can switch through the
 * presets and experience the effect on each instrument"), the part's four effect sends (P_DIST P_CHOR P_DLY P_REV):
 * they are the PART's, as its Level and Pan are; what a preset or a saved sound holds in those fields is not loaded.
 * One exception, the same day ("Digitone presets should save and load the drive level with them"): an FM TONE sound's
 * P_DIST is its Drive; the load hands it over beside this rule (cr_ui.c cu_drive_load) */
static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || (i >= P_AMODE && i <= P_SGATE) ||
           (i >= P_SLCR && i <= P_SLDEPTH) || i == P_CHRD || i == P_VOIC || (i >= P_DIST && i <= P_REV);
}
static void load_pat16(track_t *t, const uint8_t *note, const uint8_t *flags) { (void)t; (void)note; (void)flags; }
static void load_grid16(track_t *t, const uint8_t *hit, const uint8_t *acc) { (void)t; (void)hit; (void)acc; }
#if !FELUCCA_FM4
/* ui.c fm4_apply: a DIGITAL sound (engine 1, retired) converted to FM6 with its own patch */
static void fm4_apply(track_t *t, int16_t *p)
{
    uint8_t v[FP_SIZE + 1u];
    uint32_t pr = fm4_convert(p, v), tr = trk_index(t), f;
    fm6_set_patch(tr, v);
    fm6_slot[tr] = (uint8_t)p[P_E7];
    f = motion_guard();
    memcpy(t->p, p, sizeof t->p);
    t->eng_req = ENGI_FM6;
    t->preset = (uint8_t)pr;
    motion_unguard(f);
}
#endif

/* ------------------------------------------------------------ 2. upreset.c --- */
#include "upreset.c"

static uint8_t cb_booted;
static void cr_bank_boot(void)                    /* persist_boot (after flash_ok) or cr_ui_init: once */
{
    if (cb_booted)
        return;
    cb_booted = 1;
    up_boot();
    crf_boot();                                   /* the favourites (cr_favs.c) */
}

/* ----------------------------------------------- 3. the trims of the loud presets --- */
/* trim: a sound's level after the part's LEVEL (track_t.trim, fx.c mix_part), signed 0.5 dB steps, 0 none. The
 * factory presets below are loud enough that a held 6-note chord with the bass at the default levels went into the
 * master limiter (docs/INTEGRATION.md Defaults, "Per-sound trims"; measured with tests/va_levels.c); their trim is set
 * when the preset loads from its pool (cu_list_load), and when the user's record bound to it loads (an overwritten
 * factory preset: its edit of the same sound). 0 for every other preset, an added user preset, INIT and an engine
 * switch. The names resolve to the preset index at boot (cb_init), so a reordered engine table cannot trim the wrong
 * sound. (Before docs/PRESETS.md these were the rows of ChoralRoot's curated bank, which is gone.) */
static const struct { uint8_t engine; const char *preset; int8_t trim; } CB_TRIM[] = {
#if FELUCCA_PHASE                        /* (PHASE and VOICE: retired in 0.18, their rows with them) */
    {2u, "STRING", -16},                 /* PHASE STRING (the bank's CZ STRINGS) -8 dB */
    {2u, "ORGAN", -16},                  /* PHASE ORGAN (CZ ORGAN) -8 dB */
    {2u, "BRASS", -6},                   /* PHASE BRASS (CZ BRASS) -3 dB */
#endif
#if FELUCCA_VOICE
    {5u, "CHOIR AAH", -6},               /* VOICE CHOIR AAH (CHOIR) -3 dB */
#endif
    /* CZ-1's three (BRASS 1 -5 dB, STRINGS 2 -3 dB, PIPE ORGAN 1 -6 dB) went with 0.19's -6 dB for the whole engine
     * (cz_native.c CZ_VOICE_GAIN, docs/CZ1.md "Level"): without them a 6-note chord with the bass stays out of the
     * limiter, and none is louder than it was with its trim. */
    {0xFFu, "", 0},                      /* (no engine, no preset: the table is never empty, ChoralRoot has no row) */
};
static uint8_t cb_trim_k[NELEM(CB_TRIM)];         /* the resolved preset indices (0xFF: not found) */

static int cb_streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}
static int8_t cb_trim(uint32_t e, uint32_t k)     /* factory preset k of engine e's trim */
{
    uint32_t i;
    for (i = 0; i < NELEM(CB_TRIM); i++)
        if (CB_TRIM[i].engine == e && cb_trim_k[i] == k)
            return CB_TRIM[i].trim;
    return 0;
}

/* ------------------------------------------------------- 4. the pools --- */
/* docs/PRESETS.md: one pool per engine. Position 0 INIT (the engine's init sound, cr_ui.c cu_init_load; it cannot be
 * overwritten), then the engine's factory presets (engine_t.presets; the CZ-1's own INIT TONE is the pool's INIT, its
 * 64 Casio tones follow), each replaced by the user's record bound to it where one exists, then the records the user
 * added to the engine, in slot order. Built on demand from upreset.c's 32 records (no table: a scan of the slots).
 *
 * A record binds through two of its pattern bytes, which ChoralRoot never uses (no sequencer): note[15] =
 * CB_BIND_MARK and flags[15] = the index + 1 of the factory preset it overwrites, 0 for a preset added to the pool
 * (cb_store writes both). A record without the mark (saved by older firmware, restored, sent by a client) is an added
 * preset of its engine; so is one whose mark names no factory preset of its pool, or one an earlier slot already
 * binds (the first bound slot wins; cb_pool_check reports the duplicate). The record's engine is its pool (a DIGITAL
 * record: FM6's, as it plays; an ANALOG record (engine 0, retired in 0.14): the VA's, where it plays its macros on the
 * VA's init patch; another retired engine's (PHASE, LOFI, VOICE, TRIO, NOISE, PHYS since 0.18; SAMPLE, GRAIN, DRUM): the VA's,
 * where it loads as INIT, a stand-in: CB_RETIRED_POOL). A record whose engine
 * is not its pool's binds no factory preset there but a DIGITAL one (an added one: an ANALOG STRINGS overwrite does
 * not replace the VA's twelfth preset). A drum grid record (UP_VER_GRID)
 * keeps lane hits in those bytes: never bound (no DRUM in ChoralRoot). up_valid is unchanged: older firmware reads
 * a bound record as an ordinary one, whose last step holds a note of 38 (0xA6 & 127) it never plays here. */
#define CB_BIND_MARK 0xA6u
#define PF_NONE 0xFFu
enum { PK_INIT, PK_FACTORY, PK_USER };
typedef struct { uint8_t kind, fk, slot; } pool_ent_t;   /* fk: the factory preset index (PK_FACTORY; PK_USER: the
                                                          * one it overwrites, PF_NONE added); slot: PK_USER's */

static uint32_t pool_f0(uint32_t e)               /* the engine's first factory preset in its pool */
{
#if FELUCCA_CZ
    if (e == ENGI_CZ)
        return 1u;                                /* (preset 0, INIT TONE: the pool's INIT) */
#endif
#if FELUCCA_PROPHET
    if (e == ENGI_PROPHET)
        return 1u;                                /* (preset 0, INIT P5: the pool's INIT) */
#endif
    (void)e;
    return 0u;
}
static uint32_t pool_nf(uint32_t e)               /* the factory presets in the pool */
{
    uint32_t n = ENGINES[e % NENGINES]->npresets;
    return n > pool_f0(e) ? n - pool_f0(e) : 0u;
}
#define CB_RETIRED_POOL (FELUCCA_ANALOG ? 0u : ENGI_VA)   /* a retired engine's records (cr_ui.c cu_load_user) */
/* what the binding needs of slot k's record: its engine, step 16's two bytes, a drum grid. UP_POOL: upool.c's name table
 * (RAM, POOL section), never the record in flash; else the RAM mirror */
static void cb_rec_raw(uint32_t k, uint32_t *e, uint32_t *n15, uint32_t *f15, int *grid)
{
#if UP_POOL
    const upn_t *n = &upn[k % UP_SLOTS];
    *e = n->engine;
    *n15 = n->n15;
    *f15 = n->f15;
    *grid = (n->flags & UPN_GRID) != 0;
#else
    const up_rec_t *r = up_rec(k);
    *e = r->engine;
    *n15 = r->note[15];
    *f15 = r->flags[15];
    *grid = up_grid(r);
#endif
}
static uint32_t cb_pool_of(uint32_t e)              /* the pool a record of engine e is in */
{
    if (eng_ok(e))
        return e;
    return e == ENGI_DIGITAL ? ENGI_FM6 : CB_RETIRED_POOL;
}
static uint32_t cb_bind_of(uint32_t k)              /* the factory preset slot k's record names, PF_NONE */
{
    uint32_t re, n15, f15, e, f;
    int grid;
    cb_rec_raw(k, &re, &n15, &f15, &grid);
    if (!up_used(k) || grid || n15 != CB_BIND_MARK || !f15)
        return PF_NONE;
    e = cb_pool_of(re);
    if (e != re && re != ENGI_DIGITAL)
        return PF_NONE;                           /* (ANALOG's, a retired engine's record: added to the pool it plays
                                                   * in; a DIGITAL one binds in FM6's, as it converts) */
    f = f15 - 1u;
    return f >= pool_f0(e) && f < ENGINES[e]->npresets ? f : PF_NONE;
}
/* cbc from the table (3y60): once after a change, O(slots^2) byte compares in RAM, no flash */
static void cb_cache(void)
{
    uint8_t raw[UP_SLOTS];
    uint32_t k, j, re, n15, f15;
    int grid;
    if (cbc.ok && cbc.gen == up_gen)
        return;
    memset(cbc.nadd, 0, sizeof cbc.nadd);
    for (k = 0; k < UP_SLOTS; k++) {
        cb_rec_raw(k, &re, &n15, &f15, &grid);
        cbc.eng[k] = (uint8_t)cb_pool_of(re);
        raw[k] = (uint8_t)cb_bind_of(k);
        cbc.bnd[k] = raw[k];
        for (j = 0; raw[k] != PF_NONE && j < k; j++)
            if (raw[j] == raw[k] && cbc.eng[j] == cbc.eng[k])
                cbc.bnd[k] = PF_NONE;             /* (the first bound slot wins: this one is added) */
        if (up_used(k) && cbc.bnd[k] == PF_NONE && cbc.eng[k] < NENGINES)
            cbc.nadd[cbc.eng[k]]++;
    }
    cbc.gen = up_gen;
    cbc.ok = 1;
}
static uint32_t cb_rec_engine(uint32_t k)          /* the pool a used slot's record is in */
{
    cb_cache();
    return cbc.eng[k % UP_SLOTS];
}
static uint32_t cb_bind_raw(uint32_t k) { return k < UP_SLOTS ? cb_bind_of(k) : PF_NONE; }
static uint32_t cb_bound(uint32_t k)               /* .. unless an earlier slot binds it already (then: added) */
{
    cb_cache();
    return k < UP_SLOTS ? cbc.bnd[k] : PF_NONE;
}
static int cb_added(uint32_t e, uint32_t k) { return up_used(k) && cb_rec_engine(k) == e && cb_bound(k) == PF_NONE; }

static uint32_t pool_count(uint32_t e)             /* INIT + the factory presets + the added ones */
{
    cb_cache();
    return 1u + pool_nf(e) + (e < NENGINES ? cbc.nadd[e] : 0u);
}
static uint32_t pool_bound_slot(uint32_t e, uint32_t f)   /* the slot bound to factory preset f, UP_SLOTS none */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        if (up_used(k) && cb_rec_engine(k) == e && cb_bound(k) == f)
            return k;
    return UP_SLOTS;
}
static pool_ent_t pool_entry(uint32_t e, uint32_t pos)    /* past the end: INIT */
{
    pool_ent_t r = {PK_INIT, PF_NONE, PF_NONE};
    uint32_t nf = pool_nf(e), k;
    if (!pos)
        return r;
    if (pos <= nf) {
        r.fk = (uint8_t)(pool_f0(e) + pos - 1u);
        k = pool_bound_slot(e, r.fk);
        r.kind = k < UP_SLOTS ? PK_USER : PK_FACTORY;
        r.slot = (uint8_t)(k < UP_SLOTS ? k : PF_NONE);
        return r;
    }
    pos -= nf + 1u;
    for (k = 0; k < UP_SLOTS; k++)
        if (cb_added(e, k) && !pos--) {
            r.kind = PK_USER;
            r.slot = (uint8_t)k;
            return r;
        }
    return r;
}
static uint32_t pool_pos_factory(uint32_t e, uint32_t f) { return f >= pool_f0(e) ? f - pool_f0(e) + 1u : 0u; }
static uint32_t pool_pos_slot(uint32_t e, uint32_t k)     /* the place of used slot k in pool e (0: not in it) */
{
    uint32_t j, f, n;
    if (k >= UP_SLOTS || !up_used(k) || cb_rec_engine(k) != e)
        return 0u;
    f = cb_bound(k);
    if (f != PF_NONE)
        return pool_pos_factory(e, f);
    n = 1u + pool_nf(e);
    for (j = 0; j < k; j++)
        n += (uint32_t)cb_added(e, j);
    return n;
}
static uint32_t pool_pos_new(uint32_t e, uint32_t k)      /* the place a preset added into free slot k will take */
{
    uint32_t j, n = 1u + pool_nf(e);
    for (j = 0; j < k && j < UP_SLOTS; j++)
        n += (uint32_t)cb_added(e, j);
    return n;
}
static void pool_name(uint32_t e, uint32_t pos, char *b)  /* the entry's name (b holds 13) */
{
    pool_ent_t en = pool_entry(e, pos);
    if (en.kind == PK_USER)
        up_name(en.slot, b);
    else if (en.kind == PK_FACTORY)
        str_cpy(b, ENGINES[e]->presets[en.fk].name, 13);
    else
        str_cpy(b, "INIT", 13);
}
static uint32_t cb_free_slot(void)                 /* the first free user slot, UP_SLOTS: none */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS && up_used(k); k++)
        ;
    return k;
}

/* the part's sound -> slot k as a record of its engine bound to factory preset index bind - 1 (0: added to the pool),
 * named name (0 or "": the automatic name); every parameter, no pattern (ChoralRoot has none: the two binding bytes
 * are the only pattern bytes set). A bass is saved MONO (a chord part loads it POLY, cu_load_user). up_put's result
 * (0 saved, 2 flash error, 3 RAM only) */
static int cb_store_s(const track_t *t, uint32_t part, uint32_t k, const char *name, uint32_t bind,
                      const int16_t *sends);
static int cb_store(const track_t *t, uint32_t part, uint32_t k, const char *name, uint32_t bind)
{
    return cb_store_s(t, part, k, name, bind, 0);
}
/* .. sends (0: the track's): P_CHOR P_DLY P_REV as these (an FM TONE sound that carries its sends on the chord part:
 * the amounts, not what FX off leaves the track, cr_ui.c cu_store) */
static int cb_store_s(const track_t *t, uint32_t part, uint32_t k, const char *name, uint32_t bind,
                      const int16_t *sends)
{
    up_rec_t r;
    uint32_t i;
    memset(&r, 0, sizeof r);
    r.used = UP_USED;
    r.ver = UP_VER;
    r.engine = t->eng_req;
    r.np = P_COUNT;
    up_set_name(&r, k, name);
    for (i = 0; i < P_COUNT; i++) {
        int16_t v = t->p[i];
        if (i == P_VOICE && part && v != V_MONO && v != V_LEGATO)
            v = V_MONO;
        if (sends && i >= P_CHOR && i <= P_REV)
            v = sends[i - P_CHOR];
        up_set_value(&r, i, (int16_t)clamp(v, -64, 127));
    }
    r.note[15] = CB_BIND_MARK;                    /* the binding (docs/PRESETS.md "Storage") */
    r.flags[15] = (uint8_t)bind;
    return up_put(k, &r);
}

/* the emulator's trace: a duplicate binding (the later slot is an added preset) */
static void cb_pool_check(void)
{
#if defined(CR_TRACE) && CR_TRACE
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        if (cb_bind_raw(k) != PF_NONE && cb_bound(k) == PF_NONE)
            printf("pool: slot %u binds %s %u again: an added preset\n", (unsigned)k + 1u,
                   ENGINES[cb_rec_engine(k)]->name, (unsigned)cb_bind_raw(k) + 1u);
#endif
}

static void cb_init(void)
{
    uint32_t i, k;
    for (i = 0; i < NELEM(CB_TRIM); i++) {
        const engine_t *e = ENGINES[CB_TRIM[i].engine % NENGINES];
        cb_trim_k[i] = 0xFFu;
        for (k = 0; k < e->npresets; k++)
            if (cb_streq(e->presets[k].name, CB_TRIM[i].preset)) {
                cb_trim_k[i] = (uint8_t)k;
                break;
            }
    }
    cb_pool_check();
}
