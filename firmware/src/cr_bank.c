/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the sounds (docs/INTEGRATION.md section 7).
 *
 *   1. what upreset.c (Felucca's 32 user slots, kept unedited) calls of Felucca's dropped UI (ui.c), with
 *      ChoralRoot's meaning: no undo copy (load_begin / load_end), no pattern (load_pat16 / load_grid16), the
 *      messages through cr_ui.c (ui_message, ui_say: prototypes here, bodies in cr_ui.c), `ui` (main.c's fields);
 *   2. upreset.c itself (and fm6_bank.c, which it includes);
 *   3. the ChoralRoot factory bank: 24 chord sounds for PRESETS and 8 basses for ALGORITHM (with FELUCCA_VA 16 and 4
 *      more after them, the VA's: eng_va.c), picked from Felucca's
 *      engines by preset name and engine character (pads, EPs, organs, strings, keys, plucks), as a table of
 *      (engine, Felucca preset name, display name, level trim); the name resolves to the preset index at boot
 *      (cb_boot), so a reordered engine table cannot load the wrong sound;
 *   4. the lists PRESETS / ALGORITHM browse: the bank first, then the used user slots (ALGORITHM: the user sounds
 *      saved MONO / LEGATO, i.e. basses).
 * Included before cr_ui.c, after storage.c (device) and cr_anim.c. */

/* ------------------------------------------------ 1. upreset.c's UI names --- */
static struct {
    uint8_t home, force, uboot, page;   /* main.c: breadcrumbs, a full redraw, the UPDATE MODE countdown */
    uint8_t ppick;                      /* (upreset.c: SEQ > PATTERNS' pick; nothing reads it here) */
} ui;
static void ui_message(const char *s);            /* cr_ui.c: a message box */
static void ui_say(const char *a, const char *b);
static uint32_t up_gen;                           /* bumped on every user bank change */
static uint8_t sync_reload;                       /* (Felucca's editor RELOAD push: no editor) */
enum { UNDO_SOUND = 1, UNDO_PAT = 2 };
static void load_begin(track_t *t, uint32_t what) { (void)t; (void)what; }   /* ChoralRoot keeps no undo copy */
static void load_end(track_t *t) { (void)t; }
static int chain_busy(void) { return chain.running || chain.armed; }
static int transport_busy(void)                   /* no flash erase while Felucca's transport runs (inert here) */
{
    int busy;
    fm1_irq_off();
    busy = song.playing || chain_busy() || transport_req == 1u;
    fm1_irq_on();
    return busy;
}
/* ui.c param_kept: the part's own parameters, which a sound load leaves (the mix, the arp, the sequencer) */
static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || (i >= P_AMODE && i <= P_SGATE) ||
           (i >= P_SLCR && i <= P_SLDEPTH) || i == P_CHRD || i == P_VOIC;
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
}

/* --------------------------------------------------- 3. the factory bank --- */
#define CB_ANALOG 0u
#define CB_PHASE 2u
#define CB_LOFI 3u
#define CB_SAMPLE 4u
#define CB_VOICE 5u
#define CB_TRIO 6u
#define CB_WHEEL 7u
#define CB_GRAIN 8u
#define CB_PHYS 9u
#define CB_FM6 12u
#define CB_VA ENGI_VA            /* the VA engine (eng_va.c, 13): its sounds after Felucca's */
/* trim: the sound's level after the part's LEVEL (track_t.trim, fx.c mix_part), signed 0.5 dB steps, 0 none; set
 * when the bank's sound loads (cu_list_load), 0 for a user sound, an engine preset or INIT. LEVEL stays the part's
 * (the MIX page: kept across loads); the trim evens out the bank's loud sounds so a held 6-note chord with the bass
 * at the default levels stays under the master limiter (limited <= 10 % of the hold with SUB BASS, docs/INTEGRATION.md
 * Defaults) */
typedef struct { uint8_t engine; const char *preset, *name; int8_t trim; } cb_entry_t;
static const cb_entry_t CB_CHORD[] = {           /* PRESETS 01..24 */
    {CB_FM6, "TINE EP", "TINE EP"},       {CB_FM6, "BELL", "FM BELL"},
    {CB_SAMPLE, "PIANO", "PIANO"},        {CB_FM6, "PAD", "FM PAD"},
    {CB_ANALOG, "SOFT PAD", "SOFT PAD"},  {CB_ANALOG, "STRINGS", "STRINGS", -2},
    {CB_ANALOG, "PWM STR", "PWM STRINGS"},{CB_PHASE, "STRING", "CZ STRINGS", -16},
    {CB_WHEEL, "FULL ORGAN", "FULL ORGAN"},{CB_WHEEL, "GOSPEL", "GOSPEL ORGAN"},
    {CB_WHEEL, "JAZZ PERC", "JAZZ ORGAN"},{CB_FM6, "ORGAN", "FM ORGAN"},
    {CB_PHASE, "ORGAN", "CZ ORGAN", -16},      {CB_VOICE, "CHOIR AAH", "CHOIR", -6},
    {CB_GRAIN, "CLOUD PAD", "CLOUD PAD"}, {CB_GRAIN, "SHIMMER", "SHIMMER"},
    {CB_ANALOG, "BRASS", "SYNTH BRASS"},  {CB_PHASE, "BRASS", "CZ BRASS", -6},
    {CB_FM6, "MARIMBA", "MARIMBA"},       {CB_PHYS, "HARP", "HARP"},
    {CB_PHYS, "KALIMBA", "KALIMBA"},      {CB_ANALOG, "PLUCK", "PLUCK"},
    {CB_FM6, "PLUCK", "FM PLUCK"},        {CB_ANALOG, "SINE KEY", "SINE KEYS"},
#if FELUCCA_VA                                   /* PRESETS 25..40: the VA's chord sounds (levels set in the preset) */
    {CB_VA, "LUSH PAD", "LUSH PAD"},      {CB_VA, "WARM PAD", "WARM PAD"},
    {CB_VA, "GLASS PAD", "GLASS PAD"},    {CB_VA, "SLOW STRINGS", "SLOW STRINGS"},
    {CB_VA, "ENSEMBLE STR", "ENSEMBLE STR"}, {CB_VA, "SYNTH BRASS", "VA BRASS"},
    {CB_VA, "SOFT BRASS", "SOFT BRASS"},  {CB_VA, "POLY KEYS", "POLY KEYS"},
    {CB_VA, "PWM KEYS", "PWM KEYS"},      {CB_VA, "CLAV", "CLAV"},
    {CB_VA, "SOFT LEAD", "SOFT LEAD"},    {CB_VA, "HOLLOW", "HOLLOW"},
    {CB_VA, "BELLS", "BELLS"},            {CB_VA, "SWEEP PAD", "SWEEP PAD"},
    {CB_VA, "SOFT AAH", "SOFT AAH"},      {CB_VA, "ORGANISH", "ORGANISH"},
#endif
};
static const cb_entry_t CB_BASS[] = {            /* ALGORITHM 1..8 (0 = OFF) */
    {CB_ANALOG, "SUB BASS", "SUB BASS"},  {CB_ANALOG, "SQR BASS", "SQUARE BASS"},
    {CB_FM6, "FM BASS", "FM BASS"},       {CB_TRIO, "FAT BASS", "FAT BASS"},
    {CB_ANALOG, "ACID", "ACID BASS"},     {CB_LOFI, "WAVE BASS", "WAVE BASS"},
    {CB_VOICE, "WOW BASS", "WOW BASS"},   {CB_PHYS, "PLUCK", "PLUCK BASS"},
#if FELUCCA_VA                                   /* ALGORITHM 9..12: the VA's basses */
    {CB_VA, "DEEP SUB", "DEEP SUB"},      {CB_VA, "PUNCH BASS", "PUNCH BASS"},
    {CB_VA, "RUBBER BASS", "RUBBER BASS"},{CB_VA, "SYNC BASS", "SYNC BASS"},
#endif
};
#define CB_NCHORD ((uint32_t)NELEM(CB_CHORD))
#define CB_NBASS ((uint32_t)NELEM(CB_BASS))
static uint8_t cb_chord_p[NELEM(CB_CHORD)], cb_bass_p[NELEM(CB_BASS)];   /* the resolved preset indices */

static int cb_streq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}
static uint8_t cb_resolve(const cb_entry_t *en)   /* the preset named en->preset of its engine (none: 0) */
{
    const engine_t *e = ENGINES[en->engine % NENGINES];
    uint32_t i;
    for (i = 0; i < e->npresets; i++)
        if (cb_streq(e->presets[i].name, en->preset))
            return (uint8_t)i;
    return 0;
}
static void cb_init(void)
{
    uint32_t i;
    for (i = 0; i < CB_NCHORD; i++)
        cb_chord_p[i] = cb_resolve(&CB_CHORD[i]);
    for (i = 0; i < CB_NBASS; i++)
        cb_bass_p[i] = cb_resolve(&CB_BASS[i]);
}

/* ----------------------------------------------------------- 4. the lists --- */
/* a user slot holds a bass: saved MONO or LEGATO (a bass part's sound is MONO) */
static int cb_user_bass(uint32_t k)
{
    int16_t v;
    if (!up_used(k))
        return 0;
    v = up_rec(k)->np == P_COUNT ? up_value(up_rec(k), P_VOICE) : V_POLY;   /* (older layouts: chords) */
    return v == V_MONO || v == V_LEGATO;
}
static uint32_t cb_users(int bass)                /* used slots in the list */
{
    uint32_t k, n = 0;
    for (k = 0; k < UP_SLOTS; k++)
        n += (uint32_t)(bass ? cb_user_bass(k) : up_used(k));
    return n;
}
/* PRESETS: 0..CB_NCHORD-1 the bank, then the used slots; ALGORITHM: 0..CB_NBASS-1, then the bass slots */
static uint32_t cb_count(int bass) { return (bass ? CB_NBASS : CB_NCHORD) + cb_users(bass); }
/* the slot at list position pos (pos past the bank), UP_SLOTS: none */
static uint32_t cb_slot_at(int bass, uint32_t pos)
{
    uint32_t k, n0 = bass ? CB_NBASS : CB_NCHORD;
    if (pos < n0)
        return UP_SLOTS;
    pos -= n0;
    for (k = 0; k < UP_SLOTS; k++)
        if ((bass ? cb_user_bass(k) : up_used(k)) && !pos--)
            return k;
    return UP_SLOTS;
}
static uint32_t cb_pos_of_slot(int bass, uint32_t slot)   /* the list position of user slot `slot` */
{
    uint32_t k, n = bass ? CB_NBASS : CB_NCHORD;
    for (k = 0; k < slot && k < UP_SLOTS; k++)
        n += (uint32_t)(bass ? cb_user_bass(k) : up_used(k));
    return n;
}
static uint32_t cb_find(int bass, const char *name)       /* the bank entry named so (none: 0) */
{
    uint32_t i, n = bass ? CB_NBASS : CB_NCHORD;
    for (i = 0; i < n; i++)
        if (cb_streq((bass ? CB_BASS : CB_CHORD)[i].name, name))
            return i;
    return 0;
}
/* the name at list position pos: the bank's or the slot's (b holds 13) */
static void cb_name(int bass, uint32_t pos, char *b)
{
    uint32_t k;
    if (pos < (bass ? CB_NBASS : CB_NCHORD)) {
        str_cpy(b, (bass ? CB_BASS : CB_CHORD)[pos].name, 13);
        return;
    }
    k = cb_slot_at(bass, pos);
    if (k < UP_SLOTS)
        up_name(k, b);
    else
        str_cpy(b, "-", 13);
}
