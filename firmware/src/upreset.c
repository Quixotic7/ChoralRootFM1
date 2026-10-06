/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* User presets (editor protocol v2, cmds 16-21; the SAVE > USER page): 32
 * slots in two storage.c objects (OBJ_UPRESET0/1, A/B sectors at
 * 0xDC000..0xDFFFF), 16 records each, mirrored in RAM so browsing never
 * reads flash. A record: engine, name, the instrument parameters, a 16-step
 * pattern (factory PATTERNS[] format). Loading one loads the sound only; its pattern
 * is offered by SEQ > PATTERNS ("U07", up_pat_load). The format is unchanged.
 *
 * Versions: a bank whose magic, record size or slot count differ reads as
 * empty; so does a record with another layout version. A record keeps np =
 * the P_COUNT it was stored with; when that differs from today's it is
 * mapped by count: its last 8 values are P_E0..P_E7, the first np - 8 are
 * P_LEVEL.. in order, and parameters it does not have take their defaults.
 * That holds as long as common parameters are only ever added just before
 * P_E0 (else bump UP_VER and translate). Version 1 (before 1.0) has the
 * same layout; only its PHYS MODEL 2 meant DUST, which up_values loads as
 * MODAL bowed (eng_phys.c phys_legacy). A record of PHYS MODEL 4 (DRUM, version 2
 * until the kit became the DRUM engine) is that engine: up_migrate rewrites it
 * in the RAM mirror when a bank is read and when UP_PUT sends one (core.h
 * drum_from_phys); flash keeps the old record until its bank is written again,
 * which stores the new one. No version bump: today's PHYS has no MODEL 4, so
 * such a record cannot be anything else, and older firmware keeps reading the bank.
 * Version 3 (UP_VER_GRID, since the DRUM grid) is the same record with a drum grid as its pattern: note[i] is
 * step i's lane hits (bit l = lane l), flags[i] their accents. A DRUM track whose first 16 steps strike a
 * lane is stored so (its notes on their lanes); every other sound as version 2, which older firmware reads.
 * Older firmware shows a version 3 record as empty and keeps its bytes.
 * A record of engine 1 (DIGITAL, retired in 1.0) stays as it is (UP_PUT takes it too): its values are DIGITAL's,
 * and every load converts them to an FM6 sound with its own patch (ui.c fm4_apply, fm4_convert.c); lists count it
 * with FM6's (up_engine).
 *
 * With -DUP_HOST (host test) only the part above #ifndef UP_HOST is built;
 * it needs nothing but core.h. */
#define UP_PER_BANK 16u
#define UP_PMAX 72u                              /* room for P_COUNT to grow */
#define UP_USED 0xA5u
#define UP_VER 4u                                /* 2 since 1.0; 1 is read too (PHYS MODEL 2 was DUST) */
#define UP_VER_GRID 5u                           /* 2 with a drum grid as the pattern (see the top) */
#define UP_BANK_MAGIC 0x31425055u                /* "UPB1" */
typedef struct {
    uint8_t used, ver, engine, np;               /* UP_USED, UP_VER, engine, P_COUNT when stored */
    char name[12];                               /* ASCII 32..126, 0-padded (no 0 when 12 long) */
    union { int16_t p[UP_PMAX]; uint8_t packed[UP_PMAX * 2u]; };
    uint8_t note[16], flags[16];                 /* note 0 = rest; flags 1 accent, 2 slide, 4 tie (UP_VER_GRID:
                                                  * lane hits, their accents) */
} up_rec_t;
typedef struct {
    uint32_t magic;
    uint16_t rsize, nslot;
    up_rec_t r[UP_PER_BANK];
} up_bank_t;
_Static_assert(sizeof(up_rec_t) == 192, "user preset record layout");
_Static_assert(P_COUNT <= UP_PMAX * 2u && P_COUNT < 128, "user preset record: P_COUNT");
static up_bank_t up_bank[UP_SLOTS / UP_PER_BANK];

static up_rec_t *up_rec(uint32_t k) { return &up_bank[k / UP_PER_BANK].r[k % UP_PER_BANK]; }

static int up_valid(const up_rec_t *r)
{
    if (!(r->used == UP_USED && r->ver >= 1u && r->ver <= UP_VER_GRID && r->engine < NENGINES &&
          r->np >= 8u && r->np <= (r->ver >= 4u ? UP_PMAX * 2u : UP_PMAX) && r->name[0])) return 0;
    if (r->ver >= 4u) for (uint32_t i = 0; i < r->np; i++) if (r->packed[i] > 191u) return 0;
    return 1;
}

static int up_used(uint32_t k) { return k < UP_SLOTS && up_valid(up_rec(k)); }

static int up_grid(const up_rec_t *r) { return r->ver == 3u || r->ver == UP_VER_GRID; }
static int16_t up_value(const up_rec_t *r, uint32_t k) { return r->ver >= 4u ? (int16_t)r->packed[k] - 64 : r->p[k]; }
static void up_set_value(up_rec_t *r, uint32_t k, int16_t v)
{
    if (r->ver >= 4u) r->packed[k] = (uint8_t)(v + 64); else r->p[k] = v;
}
/* Legacy PHYS drums map through decoded parameters, not their disk representation. */
static void up_migrate(up_rec_t *r)
{
    int16_t e[8]; uint32_t k;
    if (!up_valid(r)) return;
    for (k = 0; k < 8u; k++) e[k] = up_value(r, r->np - 8u + k);
    if (drum_from_phys(r->engine, e)) {
        r->engine = ENGI_DRUM;
        for (k = 0; k < 8u; k++) up_set_value(r, r->np - 8u + k, e[k]);
    }
}

static void up_bank_check(uint32_t b, int len)  /* after loading bank b (len bytes, -1 = none): wrong shape -> empty */
{
    up_bank_t *bk = &up_bank[b];
    uint32_t i;
    if (len != (int)sizeof *bk || bk->magic != UP_BANK_MAGIC || bk->rsize != sizeof(up_rec_t) ||
        bk->nslot != UP_PER_BANK)
        memset(bk, 0, sizeof *bk);
    for (i = 0; i < UP_PER_BANK; i++)
        if (up_valid(&bk->r[i]))
            up_migrate(&bk->r[i]);
}

/* the record's values in today's P_* order (mapped by count, see above); def = the defaults */
static void up_params(const up_rec_t *r, int16_t *out, const int16_t *def)
{
    int16_t values[UP_PMAX * 2u];
    for (uint32_t i = 0; i < r->np && i < NELEM(values); i++) values[i] = up_value(r, i);
    params_by_count(out, values, r->np, def);
}

static int up_name_ok(const uint8_t *s, uint32_t n)   /* 1..12 printable ASCII */
{
    uint32_t i;
    if (!n || n > 12u)
        return 0;
    for (i = 0; i < n; i++)
        if (s[i] < 32u || s[i] > 126u)
            return 0;
    return 1;
}

static void up_name(uint32_t k, char *b)       /* upper case, 0-terminated: b holds 13 */
{
    const up_rec_t *r = up_rec(k);
    uint32_t i;
    for (i = 0; i < 12u && r->name[i]; i++)
        b[i] = r->name[i] >= 'a' && r->name[i] <= 'z' ? (char)(r->name[i] - 32) : r->name[i];
    b[i] = 0;
}

static void up_pat_norm(uint8_t *note, uint8_t *flags)   /* tie: no note; rest: no flags */
{
    *note &= 127u;
    if (*flags & 4u) {
        *note = 0;
        *flags = 4;
    } else {
        *flags = *note ? (uint8_t)(*flags & (SF_ACCENT | SF_SLIDE)) : 0u;
    }
}

static void up_pat_from(up_rec_t *r, const step_t *st)   /* the first 16 steps -> the pattern */
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        r->note[i] = st[i].time == ST_NOTE && st[i].n ? st[i].note[0] : 0u;
        r->flags[i] = st[i].time == ST_TIE ? 4u : st[i].flags;
        up_pat_norm(&r->note[i], &r->flags[i]);
    }
}

static int up_pat_empty(const up_rec_t *r)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        if (r->note[i])
            return 0;
    return 1;
}

/* UP_PUT arguments: slot, engine, name, P_COUNT x v14, 16 x (note, flags) [, kind, 16 x hi] -> *r (values not
 * yet clamped); 0 ok, 1 bad arguments. *slot gets the slot byte when there is one. kind 1 (and its 16 bytes):
 * a drum grid, the pairs the low 7 bits of each step's hits and accents, hi bit 0 / 1 their bit 7 (lane 8) */
static int up_parse(const uint8_t *a, uint32_t na, up_rec_t *r, uint32_t *slot)
{
    uint32_t i, n, k, end;
    if (na < 3u)
        return 1;
    *slot = a[0];
    for (n = 0; 2u + n < na && a[2 + n]; n++)
        ;
    k = 3u + n;                                  /* after the name's 0 */
    end = k + 2u * P_COUNT + 32u;
    if (a[0] >= UP_SLOTS || a[1] >= NENGINES || 2u + n >= na || !up_name_ok(a + 2, n) ||
        (na != end && (na != end + 17u || a[end] > 1u)))
        return 1;
    up_rec_t staged, *target = r;
    r = &staged;
    memset(r, 0, sizeof *r);
    r->used = UP_USED;
    r->ver = UP_VER;
    r->engine = a[1];
    r->np = P_COUNT;
    for (i = 0; i < n; i++)
        r->name[i] = (char)a[2 + i];
    for (i = 0; i < P_COUNT; i++, k += 2u)
        {
            int32_t value = (int32_t)((a[k] & 127u) | (a[k + 1] & 127u) << 7) - 8192;
            if (value < -64 || value > 127) return 1;
            up_set_value(r, i, (int16_t)value);
        }
    if (na >= k + 33u + 16u && a[k + 32u] == 1u) {    /* a drum grid */
        r->ver = UP_VER_GRID;
        for (i = 0; i < 16u; i++) {
            uint32_t hi = a[k + 33u + i];
            r->note[i] = (uint8_t)((a[k + 2u * i] & 127u) | (hi & 1u) << 7);
            r->flags[i] = (uint8_t)(((a[k + 2u * i + 1u] & 127u) | (hi & 2u) << 6) & r->note[i]);
        }
        *target = *r;
        return 0;
    }
    for (i = 0; i < 16u; i++, k += 2u) {
        r->note[i] = a[k];
        r->flags[i] = a[k + 1];
        up_pat_norm(&r->note[i], &r->flags[i]);
    }
    up_migrate(r);                               /* (an editor of before the DRUM engine) */
    *target = *r;
    return 0;
}

#ifndef UP_HOST
#if FELUCCA_VA
#include "va_store.c"                          /* ChoralRoot: the VA patches, one per slot (the hooks below) */
static uint8_t up_va_keep;                     /* up_rename: the slot's stored VA patch stays */
#endif
static void up_values(const up_rec_t *r, int16_t *v)   /* mapped and clamped for its engine */
{
    int16_t def[P_COUNT];
    uint32_t i;
#if FELUCCA_VA
    va_store_loading(r);                       /* a VA record: its slot's patch on the load (va_track_loaded) */
#endif
    for (i = 0; i < P_COUNT; i++)
        def[i] = param_desc_of(r->engine, i)->def;
    up_params(r, v, def);
    if (r->ver == 1u && ENGINES[r->engine] == &ENG_PHYS)   /* (before 1.0: MODEL 2 was DUST) */
        phys_legacy(&v[P_E0]);
    for (i = 0; i < P_COUNT; i++)
        v[i] = (int16_t)clamp(v[i], param_desc_of(r->engine, i)->min, param_desc_of(r->engine, i)->max);
}

#include "fm6_bank.c"                          /* the FM6 patch bank: the same kind of store */

static void up_boot(void)                      /* persist_boot: the banks from flash */
{
#if FELUCCA_FLASH
    uint32_t b;
    for (b = 0; b < UP_SLOTS / UP_PER_BANK; b++)
        up_bank_check(b, flash_ok ? st_load(OBJ_UPRESET0 + b, &up_bank[b], sizeof up_bank[b]) : -1);
#endif
    fm6_bank_boot();
#if FELUCCA_VA
    va_store_boot();
#endif
#ifdef FELUCCA_FAVORITES
    for (uint32_t k = 0; k < UP_SLOTS; k++)
        if (!up_used(k)) favorite_set(NENGINES, k, 0);
#endif
}

/* record k = *r (0: erase), then the bank to flash: 0 ok, 1 bad slot, 2 flash error or the transport runs (nothing
 * written), 3 no flash (kept in RAM) */
static int up_put(uint32_t k, const up_rec_t *r)
{
    up_bank_t *bk;
#if FELUCCA_FLASH
    up_rec_t old;
    uint32_t magic;
    uint16_t rsize, nslot;
#endif
    if (k >= UP_SLOTS)
        return 1;
    if (transport_busy()) {                            /* no flash erase while playing (project_save) */
        ui_message("STOP TO SAVE");
        return 2;
    }
    bk = &up_bank[k / UP_PER_BANK];
#if FELUCCA_FLASH
    old = *up_rec(k);
    magic = bk->magic;
    rsize = bk->rsize;
    nslot = bk->nslot;
#endif
    bk->magic = UP_BANK_MAGIC;
    bk->rsize = sizeof(up_rec_t);
    bk->nslot = UP_PER_BANK;
    if (r)
        *up_rec(k) = *r;
    else
        memset(up_rec(k), 0, sizeof(up_rec_t));
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_UPRESET0 + k / UP_PER_BANK, bk, sizeof *bk)) {
        *up_rec(k) = old;
        bk->magic = magic;
        bk->rsize = rsize;
        bk->nslot = nslot;
        return 2;
    }
#endif
    if (!r) {
#ifdef FELUCCA_FAVORITES
        if (favorite_set(NENGINES, k, 0)) settings_save();
#endif
        uint32_t i;
        for (i = 0; i < NTRK; i++)
            if (trk[i].user == k + 1u)
                trk[i].user = 0;
    }
    up_gen++;
#if FELUCCA_VA
    if (!up_va_keep)                                   /* the VA patch of the sound saved (va_store.c), or none */
        va_store_saved(k, r);
#endif
#if FELUCCA_FLASH
    if (flash_ok)
        return 0;
#endif
    return 3;
}

static void up_slot_label(char *b, uint32_t k)  /* "U07" */
{
    b[0] = 'U';
    b[1] = (char)('0' + (k + 1u) / 10u);
    b[2] = (char)('0' + (k + 1u) % 10u);
    b[3] = 0;
}

/* the automatic name of engine e's sound in slot k: engine name + slot number ("ANALOG 07"); b holds 13 */
static void up_auto_name(char *b, uint32_t e, uint32_t k)
{
    char l[4];
    e %= NENGINES;
    str_cpy(b, ENGINES[eng_ok(e) ? e : ENGI_FM6]->name, 9);   /* (a DIGITAL record plays as FM6) */
    up_slot_label(l, k);
    str_cpy(b + str_len(b), " ", 2);
    str_cpy(b + str_len(b), l + 1, 3);
}

/* the record's name = name (at most 12), 0 or "": the automatic one */
static void up_set_name(up_rec_t *r, uint32_t k, const char *name)
{
    char b[16];
    uint32_t i;
    if (!name || !name[0]) {
        up_auto_name(b, r->engine, k);
        name = b;
    }
    memset(r->name, 0, sizeof r->name);
    for (i = 0; i < 12u && name[i]; i++)
        r->name[i] = name[i];
}

/* the selected part's sound -> slot k; name 0 or "": the automatic name (up_auto_name); up_put's result */
static int up_store(uint32_t k, const char *name)
{
    up_rec_t r;
    uint32_t i;
    memset(&r, 0, sizeof r);
    r.used = UP_USED;
    r.ver = UP_VER;
    r.engine = TSEL->eng_req;
    r.np = P_COUNT;
    up_set_name(&r, k, name);
    for (i = 0; i < P_COUNT; i++)
        up_set_value(&r, i, motion_base_value(TSEL, i));
    up_pat_from(&r, TSEL->step);
    if (drum_track(TSEL)) {                             /* a DRUM track that strikes a lane: its grid */
        uint32_t any = 0;
        for (i = 0; i < 16u; i++)
            any |= step_lanes(&TSEL->step[i]);
        if (any) {
            r.ver = UP_VER_GRID;
            for (i = 0; i < 16u; i++) {
                r.note[i] = (uint8_t)step_lanes(&TSEL->step[i]);
                r.flags[i] = (uint8_t)step_accents(&TSEL->step[i]);
            }
        }
    }
    return up_put(k, &r);
}

/* slot k renamed (name 0 or "": the automatic one), the sound and its pattern as they are; up_put's result, 1 for
 * an empty slot */
static int up_rename(uint32_t k, const char *name)
{
    up_rec_t r;
    if (!up_used(k))
        return 1;
    r = *up_rec(k);
    up_set_name(&r, k, name);
#if FELUCCA_VA
    {
        int rc;
        up_va_keep = 1;                                /* the sound as it is: its stored patch too */
        rc = up_put(k, &r);
        up_va_keep = 0;
        return rc;
    }
#else
    return up_put(k, &r);
#endif
}

/* slot k -> the selected part's sound: engine and every parameter except the track's own (param_kept:
 * the mix, ARP, SCL, the pattern parameters, the SLICER). The steps stay: the record's pattern is
 * loaded only from SEQ > PATTERNS (up_pat_load). 0 ok, 1 empty */
static int up_load(uint32_t k)
{
    const up_rec_t *r;
    int16_t v[P_COUNT];
    uint32_t i;
    track_t *t = TSEL;
    if (!up_used(k))
        return 1;
    r = up_rec(k);
    up_values(r, v);
    load_begin(t, UNDO_SOUND);                          /* (ui.c: the copy for SAVE held = undo) */
    panic_req |= (uint8_t)(1u << song.sel);
#if !FELUCCA_FM4
    if (r->engine == ENGI_DIGITAL) {                    /* a DIGITAL sound (kept as it was stored): FM6 */
        int16_t p[P_COUNT];
        for (i = 0; i < P_COUNT; i++)
            p[i] = param_kept(i) ? t->p[i] : v[i];
        fm4_apply(t, p);
    } else
#endif
    {
        fm1_irq_off();                                  /* the audio ISR must not see half a sound */
        t->eng_req = r->engine;
        for (i = 0; i < P_COUNT; i++)
            if (!param_kept(i))
                t->p[i] = v[i];
        t->preset = 0;
        fm1_irq_on();
        fm6_track_loaded(t);                            /* FM6: a user preset holds the PTCH and the macros */
    }
    t->user = (uint8_t)(k + 1u);
    load_end(t);
    sync_reload = 1;
    ui.force = 1;
    return 0;
}

/* the patterns of the user presets (SEQ > PATTERNS lists them after the factory ones, ui.c pat_count) */
static int up_has_pat(uint32_t k) { return up_used(k) && !up_pat_empty(up_rec(k)); }

static uint32_t up_pat_count(void)
{
    uint32_t k, n = 0;
    for (k = 0; k < UP_SLOTS; k++)
        n += (uint32_t)up_has_pat(k);
    return n;
}

static uint32_t up_pat_nth(uint32_t n)         /* slot of the n-th one that holds a pattern (n < up_pat_count()) */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        if (up_has_pat(k) && !n--)
            return k;
    return 0;
}

static uint32_t up_pat_rank(uint32_t slot)     /* ones that hold a pattern before it */
{
    uint32_t k, n = 0;
    for (k = 0; k < slot && k < UP_SLOTS; k++)
        n += (uint32_t)up_has_pat(k);
    return n;
}

/* slot k's pattern -> track t's steps 1..16 (the rest cleared), with the record's LEN (at most 16), DIV,
 * SWING and GATE; the sound stays (ui.c pat_load: the undo copy) */
static void up_pat_load(track_t *t, uint32_t k)
{
    const up_rec_t *r;
    int16_t v[P_COUNT];
    uint32_t i;
    if (!up_has_pat(k))
        return;
    r = up_rec(k);
    up_values(r, v);
    if (up_grid(r))
        load_grid16(t, r->note, r->flags);
    else
        load_pat16(t, r->note, r->flags);
#if FELUCCA_VA
    va_user_pending = 0;                               /* (a pattern only: no sound load follows) */
#endif
    for (i = P_SDIV; i <= P_SGATE; i++)
        t->p[i] = v[i];
    t->p[P_SLEN] = (int16_t)clamp(v[P_SLEN], 1, 16);   /* (the pattern has 16 steps) */
}

/* the engine a used slot's sound plays on (a DIGITAL record: FM6, without FELUCCA_FM4) */
static uint32_t up_engine(uint32_t k) { return eng_ok(up_rec(k)->engine) ? up_rec(k)->engine : ENGI_FM6; }

static uint32_t up_count(void)                 /* used slots */
{
    uint32_t k, n = 0;
    for (k = 0; k < UP_SLOTS; k++)
        n += (uint32_t)up_used(k);
    return n;
}

static uint32_t up_nth(uint32_t n)             /* slot of the n-th used one (n < up_count()) */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        if (up_used(k) && !n--)
            return k;
    return 0;
}

static uint32_t up_rank(uint32_t slot)         /* used slots before it */
{
    uint32_t k, n = 0;
    for (k = 0; k < slot && k < UP_SLOTS; k++)
        n += (uint32_t)up_used(k);
    return n;
}

/* SAVE > USER page actions, with the message in the top bar. name: the save's or the rename's (0 or "": automatic) */
static void up_ui_named(uint32_t op, uint32_t k, const char *name)   /* 0 load, 1 erase, 2 save, 3 rename */
{
    char l[4];
    int rc;
    up_slot_label(l, k);
    if ((op < 2u || op == 3u) && !up_used(k)) {
        ui_message("EMPTY SLOT");
        return;
    }
    if (op && (song.playing || chain_busy() || transport_req == 1u)) {    /* (up_put refuses too) */
        ui_message("STOP TO SAVE");
        return;
    }
    if (op == 0u) {
        up_load(k);
        if (up_has_pat(k))                              /* SEQ > PATTERNS starts at its pattern */
            ui.ppick = (uint8_t)(NPATTERNS + up_pat_rank(k));
        ui_say("LOADED ", l);
        return;
    }
    rc = op == 1u ? up_put(k, 0) : op == 3u ? up_rename(k, name) : up_store(k, name);
    if (rc == 3)
        ui_message(op == 1u ? "ERASED (RAM)" : op == 3u ? "RENAMED (RAM)" : "SAVED (RAM)");
    else if (rc)
        ui_message(op == 1u ? "ERASE ERROR" : "SAVE ERROR");
    else
        ui_say(op == 1u ? "ERASED " : op == 3u ? "RENAMED " : "SAVED ", l);
    ui.force = 1;
}
static void up_ui(uint32_t op, uint32_t k) { up_ui_named(op, k, 0); }
#endif
