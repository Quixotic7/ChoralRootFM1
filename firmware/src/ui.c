/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* FELUCCA user interface.
 * Flat: SURF cards and panels on the palette's background, no rules, one type family (Inter Tight, three sizes), tracks
 * named by circled numerals. Four columns <-> KNOB 1..4. Rendering is lazy:
 * every element remembers what it last drew and is redrawn only on change. */
static int project_save(uint32_t slot);
static void panel_setup(void);
static void project_load(uint32_t slot);
static int project_used(uint32_t slot);
static uint32_t chain_prepare(void);
static int up_used(uint32_t k);              /* user presets: upreset.c */
static int up_load(uint32_t k);
static uint32_t up_count(void);
static uint32_t up_nth(uint32_t n);
static uint32_t up_rank(uint32_t slot);
static uint32_t up_engine(uint32_t k);
static void up_name(uint32_t k, char *b);
static void up_slot_label(char *b, uint32_t k);
static void up_ui(uint32_t op, uint32_t k);
static int up_has_pat(uint32_t k);               /* user presets that hold a pattern */
static uint32_t up_pat_count(void);
static uint32_t up_pat_nth(uint32_t n);
static uint32_t up_pat_rank(uint32_t slot);
static void up_pat_load(track_t *t, uint32_t k);
static void up_auto_name(char *b, uint32_t e, uint32_t k);   /* naming (ui_name.c) */
static void up_ui_named(uint32_t op, uint32_t k, const char *name);
static int project_save_as(uint32_t slot, const char *name);
static int project_name(uint32_t slot, char *b);
static int project_rename(uint32_t slot, const char *name);
static void project_cur_name(char *b);
static uint32_t user_of(const track_t *t)    /* user preset slot its sound came from, UP_SLOTS = none */
{
    return t->user && up_used(t->user - 1u) ? t->user - 1u : UP_SLOTS;
}
static uint32_t up_gen;                      /* bumped on every user bank change (redraws) */
#include "favorites.c"

static uint8_t sync_reload;                  /* engine / preset / project / user preset loaded: editor RELOAD push */

#define ACC T_THEME                /* values, curves */
#define VAL(c) ((c) == ui.hot_col && ui.hot_t ? T_ACCENT : T_THEME)   /* the knob just turned: accent */
#define RATIO(d, v) ((d)->max > (d)->min ? ((int32_t)(v) - (d)->min) * 1000 / ((d)->max - (d)->min) : -1)
/* layout: header 0..24, four cards 28..72 (57 px at x 3 + 59 c),
 * the panel 76..198 (a SURF area; the graphs live in it), footer 202..240; BG between them */
#define Y_HEAD 0
#define H_HEAD 24
#define Y_LABEL 28                    /* the cards */
#define Y_SEP_END 72
#define CARD_W 57
#define CARD_H 44
#define CARD_X(c) (3 + 59 * (int32_t)(c))
#define Y_GRAPH 76                    /* the panel */
#define H_GRAPH 122
#define Y_FOOT 202
#define H_FOOT 38

static struct {
    uint8_t home;
    uint8_t page;                /* index into PAGES */
    uint8_t fam_last[FAM_COUNT]; /* last page used per family */
    uint8_t bank;                /* SEQ: 16-step bank (follows the cursor) */
    uint8_t cursor;              /* SEQ: step being edited (STEP page KNOB 1 moves it) */
    uint8_t entry_open;          /* SEQ: keys held since the first press of this entry */
    uint8_t lane;                /* SEQ > STEP on a DRUM track (the grid): the lane the keys and KNOB 3 / 4 edit */
    uint8_t hot_col, hot_t;      /* column whose knob was just turned (drawn white) */
    uint8_t menu;                /* 0 off, 1 list, 2 about + credits (HOME held) */
    uint8_t menu_sel;
    uint16_t menu_scroll;        /* continuous ABOUT + CREDITS position, pixels */
    uint32_t menu_sig, home_t0;  /* HOME press time (btn_hold) */
    uint8_t force;               /* full redraw pending */
    uint8_t msg_t;               /* transient message frames */
    uint8_t bpm_t;               /* frames the BPM stays highlighted after a SELECT turn */
    uint8_t act;                 /* action pages: the column whose action OCT+ does, + 1; 0 = none (act_col) */
    uint32_t rec_t0;             /* REC press time (transport only) */
    uint32_t seq_t0;             /* SEQ held: direct SONG entry */
    uint32_t save_t0;            /* SAVE press time (btn_hold: held = UNDO) */
    uint8_t confirm;             /* the OCT- / OCT+ dialog: CF_*, 0 = none */
    uint8_t confirm_trk;         /* the track it clears, the slot it overwrites */
    uint8_t uslot;               /* SAVE > USER: the selected user preset slot */
    uint8_t ppick;               /* SEQ > PATTERNS: the pattern picked (pat_count list index) */
    uint8_t song_row;            /* SONG: row selected, count selects the next empty row */
    uint8_t uboot;               /* main.c: seconds left before UPDATE MODE (OCT- + OCT+ held), 0 = none */
    uint32_t ly_t0;              /* the layer button's press time | 1, LY_* bits (ui_layer.c layer_gesture) */
    uint8_t ly;                  /* the layer whose button is down (LAYER_*), 0 = none */
    uint8_t layer;               /* the layer whose map is shown (LAYER_*), 0 = none */
    uint16_t pg_down;            /* page buttons down (panel ids) that act when let go */
    uint32_t layer_sig;          /* drawn-state cache of the map */
    char msg[24];
    char msg2[24];               /* a second message, shown when the first is over */
    uint32_t enc_t[NE];
    /* drawn-state cache */
    char col[4][32];
    uint32_t graph_sig, head_sig, foot_sig, frame;
    /* rolling digits (ui_draw.c roll_*): the four card values and the header BPM */
    struct {
        char from[7];            /* the value rolling out; "" = idle */
        uint8_t t0;              /* (ui.frame) of the value's last change */
        int8_t dir;              /* +1: it went up (the old digit leaves upward), -1: down */
        uint8_t sig;             /* a card: what its value is of (label, unit, track, engine, palette) */
    } roll[5];
    int16_t roll_bpm;            /* the BPM last drawn */
} ui;

enum { CF_NONE, CF_CLEAR_SEQ, CF_CLEAR_TRK, CF_OVR_PROJ, CF_OVR_USER, CF_LOAD_PAT,
       CF_DEL_ROW, CF_CLEAR_SONG, CF_INIT_SOUND, CF_CLEAR_MOTION, CF_ERASE_USER };   /* ui.confirm: REC held on
                                   * SEQ / ARP, on TRACKS; SAVE over a used slot; a pattern over the user's steps;
                                   * USER ERASE */

static const page_t *page_over;   /* a quick layer's own four knobs (ui_layer.c), while it edits or draws them */
static const page_t *cur_page(void) { return page_over ? page_over : &PAGES[ui.page]; }

/* the quick layers (ui_layer.c): a button held, the keys and KNOB 1..4 are its shortcuts, its map over the page */
enum { LAYER_NONE, LAYER_FX, LAYER_GLO, LAYER_SCL, LAYER_EDIT, LAYER_N };
static void draw_layer(void);
static const char *layer_head(void);
static uint32_t layer_leds(void);
static uint32_t layer_btn(void);

/* FM operator pages belong to DIGITAL; they never appear on other instruments (without FELUCCA_FM4: never). SLICES:
 * a SLICE track's (ui_slice.c) */
static int page_visible(uint32_t i)
{
#if FELUCCA_SLICE
    if (PAGES[i].graph == GR_SLICES)
        return ENGINES[TSEL->eng_req % NENGINES] == &ENG_SLICE;
#else
    if (PAGES[i].graph == GR_SLICES)
        return 0;
#endif
    return !(PAGES[i].fam == FAM_EDIT && PAGES[i].id[0] >= P_FM1_ATK &&
             PAGES[i].id[0] <= P_FM4_LEVEL) || (FELUCCA_FM4 && TSEL->eng_req % NENGINES == ENGI_DIGITAL);
}

static uint32_t page_first(uint32_t fam)
{
    uint32_t i;
    for (i = 0; i < NPAGES; i++)
        if (PAGES[i].fam == fam)
            return i;
    return 0;
}

/* transient message in the top bar, right of the transport and the BPM: a + b */
static void ui_say(const char *a, const char *b)
{
    uint32_t n;
    str_cpy(ui.msg, a, sizeof ui.msg);
    n = str_len(ui.msg);
    str_cpy(ui.msg + n, b, sizeof ui.msg - n);
    ui.msg_t = 40;
    ui.msg2[0] = 0;
}

static void ui_message(const char *s) { ui_say(s, ""); }

static int chain_busy(void) { return chain.running || chain.armed; }
/* Main loop only, with interrupts enabled. PLAY may be consumed between reads;
 * take one coherent snapshot before a flash operation or song preparation. */
static int transport_busy(void)
{
    int busy;
    fm1_irq_off();
    busy = song.playing || chain_busy() || transport_req == 1u;
    fm1_irq_on();
    return busy;
}
static void chain_play_ui(void)
{
    uint32_t rc = chain_prepare();
    if (!rc) {
        ui.force = 1;
    } else if (rc >= 3u) {
        char b[8] = "A EMPTY";
        b[0] = (char)('A' + rc - 3u);
        ui_say("PATTERN ", b);
    } else {
        ui_message(rc == 1u ? "ADD A SONG ROW" : "STOP FIRST");
    }
}


static void page_entered(void)
{
    const page_t *pg = cur_page();
    song.seq_mode = !ui.home && pg->fam == FAM_SEQ;
    ui.entry_open = 0;
    ui.hot_t = 0;                                /* clear the previous page's emphasis */
    ui.act = pg->graph == GR_USER ? 4u : 0u;     /* the save screen is ready for OCT+ */
    ui.force = 1;
}

static int step_on(const step_t *st) { return st->time == ST_NOTE && (st->n || st->hit); }

static void step_clear(step_t *st)
{
    st->n = 0;
    st->time = ST_REST;
    st->flags = 0;
    st->vel = 0;
    st->hit = st->acc = 0;
    st->probability = 0;
}

/* ------------------------------------------------------- the DRUM grid --- */
/* SEQ > STEP on a DRUM track is the grid: 8 lanes x the 16 steps of a page. The white keys are the
 * steps of the page shown (a tap toggles the selected lane there), black keys 1..8 select the lane (and play
 * it), black key 9 held is ACC (white keys toggle accents, their LEDs show them), black keys 10 / 11 the page
 * down / up. KNOB 1 STEP, 2 LANE, 3 HIT, 4 ACC edit the cursor step. A sound load never converts the
 * steps: the grid shows a step's notes on their lanes (eng_drum.c step_lanes) and an edit makes the lane its
 * own (grid_own). Live recording on a DRUM track writes hits (seq.c rec_note) */
static int grid_on(void) { return !ui.home && !ui.menu && !ui.confirm && cur_page()->graph == GR_ROLL && drum_track(TSEL); }   /* (STEP only: CHANCE is SC_STEP too) */

/* black key place p (seq.c key_place) held, 0 = not */
static int black_held(uint32_t p)
{
    uint32_t k;
    for (k = 0; k < 27u; k++)
        if (key_black(k) && key_place(k) == p)
            return (int)(((fm1_in.notes & ~kb_layer) >> k) & 1u);
    return 0;
}

/* lane l of step s from now on is its hit alone: the step's lane notes become hits (nothing sounds different),
 * a note of another pitch on the lane (a low tom 41) becomes the lane's own */
static void grid_own(step_t *s, uint32_t l)
{
    uint32_t k, j = 0;
    step_to_grid(s);
    for (k = 0; k < s->n; k++)
        if (drum_lane(s->note[k]) == l)
            s->hit |= (uint8_t)(1u << l);
        else
            s->note[j++] = s->note[k];
    for (k = j; k < 4u; k++)
        s->note[k] = 0;
    s->n = (uint8_t)j;
}

/* lane l of step i: on 1, off 0, toggled 2 */
static void grid_hit(track_t *t, uint32_t i, uint32_t l, uint32_t on)
{
    step_t *s = &t->step[i % NSTEP];
    uint32_t b = 1u << (l % NLANE);
    if (on == 2u)
        on = !(step_lanes(s) & b);
    if (s->time != ST_NOTE) {                    /* a REST or a TIE: an empty step (nothing to turn off) */
        if (!on)
            return;
        step_clear(s);
        s->time = ST_NOTE;
    }
    grid_own(s, l % NLANE);
    if (on) {
        s->hit |= (uint8_t)b;
    } else {
        s->hit &= (uint8_t)~b;
        s->acc &= (uint8_t)~b;
        if (!s->n && !s->hit)
            step_clear(s);
    }
}

/* the accent of lane l at step i (on 1, off 0, toggled 2); an accent on an empty lane adds the hit */
static void grid_acc(track_t *t, uint32_t i, uint32_t l, uint32_t on)
{
    step_t *s = &t->step[i % NSTEP];
    uint32_t b = 1u << (l % NLANE);
    if (on == 2u)
        on = !(step_accents(s) & b);
    if (on)
        grid_hit(t, i, l, 1);
    if (!(step_lanes(s) & b))
        return;
    grid_own(s, l % NLANE);
    if (s->flags & SF_ACCENT) {                  /* a step accent: each hit's own from now on */
        s->acc |= s->hit;
        s->flags &= (uint8_t)~SF_ACCENT;
    }
    s->acc = (uint8_t)(on ? s->acc | b : s->acc & ~b);
}

/* SEQ cursor: wraps inside the pattern length, the bank follows, a step entry ends */
static void cursor_set(int32_t c)
{
    int32_t len = TSEL->p[P_SLEN] > 0 ? TSEL->p[P_SLEN] : 1;
    ui.cursor = (uint8_t)((c % len + len) % len);
    ui.bank = (uint8_t)(ui.cursor / 16u);
    ui.entry_open = 0;
}

static void cursor_fix(void)                           /* LEN got shorter: onto the last step */
{
    if (ui.cursor >= (uint32_t)TSEL->p[P_SLEN])
        cursor_set(TSEL->p[P_SLEN] - 1);
}

static void note_name(char *b, uint32_t n)
{
    str_cpy(b, N_NOTE[n % 12u], 4);
    fmt_int(b + str_len(b), (int32_t)(n / 12u) - 1);
}

static void open_family(uint32_t fam)
{
    if (!ui.home && cur_page()->fam == fam) {          /* same button again: next page */
        uint32_t n, i = ui.page;
        for (n = 0; n < NPAGES; n++) {
            i = (i + 1u) % NPAGES;
            if (PAGES[i].fam == fam && page_visible(i)) break;
        }
        ui.page = (uint8_t)i;
    } else if (fam == FAM_SAVE) {
        uint32_t i;
        for (i = 0; i < NPAGES; i++)
            if (PAGES[i].graph == GR_USER) break;
        ui.page = (uint8_t)i;                         /* SAVE enters the sound save screen directly */
    } else {
        ui.page = ui.fam_last[fam] && PAGES[ui.fam_last[fam]].fam == fam && page_visible(ui.fam_last[fam]) ? ui.fam_last[fam]
                                                                          : (uint8_t)page_first(fam);
    }
    ui.fam_last[fam] = ui.page;
    ui.home = 0;
    page_entered();
}

/* GLO always enters the mixer from another family. Subsequent taps visit the
 * global settings, then return to the mixer; recording has no navigation role. */
static void open_global(void)
{
    uint32_t i;
    if (!ui.home && cur_page()->fam == FAM_TRK) {
        ui.page = (uint8_t)page_first(FAM_GLO);
    } else if (!ui.home && cur_page()->fam == FAM_GLO) {
        for (i = ui.page + 1u; i < NPAGES && PAGES[i].fam != FAM_GLO; i++) {}
        ui.page = (uint8_t)(i < NPAGES ? i : page_first(FAM_TRK));
    } else {
        ui.page = (uint8_t)page_first(FAM_TRK);
    }
    ui.home = 0;
    page_entered();
}

static void go_home(void)
{
    ui.home = 1;
    ui.entry_open = 0;
    ui.hot_t = 0;
    song.seq_mode = 0;
    ui.force = 1;
}

/* ------------------------------------------------------- track setup --- */
static int seq_is_empty(const track_t *t)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++)
        if (t->step[i].n || t->step[i].hit)
            return 0;
    return 1;
}

/* One-step UNDO of a load. A sound load (a factory or user preset, an engine jump, TOOLS INIT, the
 * editor's PRESET / G_ENGSEL / UP_LOAD) changes the sound only; a pattern load (SEQ > PATTERNS) changes
 * the steps and the pattern parameters only. Each first copies the track as it was; SAVE held 0.7 s
 * swaps back what the loads changed (held again: the loads again), so steps recorded after a sound
 * load, or a sound edited after a pattern load, stay as they are. One copy for all tracks: the last
 * load wins. Loads in a row on one track with nothing changed in between (the PRESETS knob through the
 * list, one pattern after the other, an editor audition) keep the copy from before the first, so the
 * undo goes back past the whole browse. Not snapshotted: power-on, projects. */
enum { UNDO_SOUND = 1, UNDO_PAT = 2 };
static struct {
    uint8_t trk;                 /* track + 1, 0 = nothing to undo */
    uint8_t keep;                /* the track is as the last load left it (undo.after): a next load keeps the copy */
    uint8_t what;                /* UNDO_SOUND | UNDO_PAT: what the loads since the copy changed (undo_swap) */
    uint8_t eng, preset, user, patn;
    uint8_t fm6_slot;            /* the track's FM6 patch and its PTCH slot (eng_fm6.c): an edited or a project's */
    uint8_t fm6[FP_SIZE + 1u];   /* patch is the track's own, not PTCH's factory one */
    int16_t p[P_COUNT];
    step_t step[NSTEP];
    motion_store_t motion_backup; /* one track only, swaps with the shared event pool on undo */
    uint32_t after;              /* track_sig right after the last load */
    uint32_t pat;                /* pat_sig[] of the copy */
    uint32_t t_ms;               /* time of the last load (the editor's SETs after it belong to it) */
} undo;
static uint8_t undo_depth;       /* loads nest (an engine jump loads its first preset): the outer one counts;
                                  * felucca_init / project_load raise it to take no copy at all */
static uint32_t pat_sig[NTRK];   /* steps_sig of the pattern the last pattern load put into each track: such
                                  * steps, untouched, are replaced by the next pattern without asking */
static uint8_t pat_last[NTRK];   /* that pattern's list index + 1, 0 = none */

static uint32_t fnv(uint32_t h, const void *p, uint32_t n)
{
    const uint8_t *b = (const uint8_t *)p;
    while (n--)
        h = (h ^ *b++) * 16777619u;
    return h;
}
static uint32_t steps_sig(const track_t *t) { return fnv(2166136261u, t->step, sizeof t->step); }
static uint32_t track_sig(const track_t *t)      /* the sound (an FM6 track's patch too), the steps */
{
    uint8_t id[3] = {t->eng_req, t->preset, t->user};
    uint32_t h = fnv(fnv(steps_sig(t), t->p, sizeof t->p), id, 3), k = trk_index(t);
    h = fnv(h, fm6_patch[k], sizeof fm6_patch[k]); /* (a patch the editor sent between two loads) */
    for (uint32_t j = 0; j < motion.count; j++)
        if ((motion.event[j].place >> 6) == k) h = fnv(h, &motion.event[j], sizeof motion.event[j]);
    return h ^ ((motion.on >> k) & 1u);
}

static void load_begin(track_t *t, uint32_t what)
{
    uint32_t i = trk_index(t);
    if (undo_depth++)
        return;
    motion_restore(t);
    if (undo.keep && undo.trk == i + 1u && track_sig(t) == undo.after) {
        undo.what |= (uint8_t)what;               /* browsing on: the copy from before the first load stays */
        motion_reset(t);
        return;
    }
    undo.trk = (uint8_t)(i + 1u);
    undo.what = (uint8_t)what;
    undo.eng = t->eng_req;
    undo.preset = t->preset;
    undo.user = t->user;
    memcpy(undo.p, t->p, sizeof undo.p);
    memcpy(undo.step, t->step, sizeof undo.step);
    memcpy(undo.fm6, fm6_patch[i], FP_SIZE);
    undo.fm6_slot = fm6_slot[i];
    undo.pat = pat_sig[i];
    undo.patn = pat_last[i];
    motion_snapshot_track(t, &undo.motion_backup);
    motion_reset(t);
}

static void load_end(track_t *t)
{
    if (--undo_depth)
        return;
    motion_rebase(t);
    undo.after = track_sig(t);
    undo.keep = 1;
    undo.t_ms = fm1_ms;
}

/* the editor's SET right after a load on the selected track (an audition: G_ENGSEL, then the patch's
 * values): part of that load, the copy from before it stays */
static void load_extend(track_t *t)
{
    if (undo.keep && undo.trk == trk_index(t) + 1u && fm1_ms - undo.t_ms < 1500u) {
        undo.after = track_sig(t);
        undo.t_ms = fm1_ms;
    }
}

static int param_kept(uint32_t i);

/* SAVE held: what the loads changed (undo.what) and the copy change places, so held again = the loads
 * again. The sound: engine, preset and its parameters (not param_kept); the pattern: the steps and LEN
 * DIV SWING GATE. The mix (LEVEL PAN MUTE) stays: no load changes it */
static void undo_swap(void)
{
    track_t *t;
    uint32_t i;
    char b[4] = {'T', 0, 0, 0};
    if (!undo.trk) {
        ui_message("NOTHING TO UNDO");
        return;
    }
    t = &trk[(undo.trk - 1u) % NTRK];
    motion_restore(t);
    motion_store_t current_motion;
    motion_snapshot_track(t, &current_motion);
    if (motion_replace_track(t, &undo.motion_backup) != 0) {
        ui_message("MOTION FULL");
        return;
    }
    undo.motion_backup = current_motion;
    fm1_irq_off();                                /* the audio ISR must not see half a sound */
    if (undo.what & UNDO_SOUND) {
        uint8_t e = t->eng_req, pr = t->preset, u = t->user;
        panic_req |= (uint8_t)(1u << trk_index(t));
        t->eng_req = undo.eng;
        t->preset = undo.preset;
        t->user = undo.user;
        undo.eng = e;
        undo.preset = pr;
        undo.user = u;
        for (i = 0; i < P_COUNT; i++)
            if (!param_kept(i)) {
                int16_t v = t->p[i];
                t->p[i] = undo.p[i];
                undo.p[i] = v;
            }
    }
    if (undo.what & UNDO_PAT) {
        uint32_t ps = pat_sig[trk_index(t)];
        uint8_t pn = pat_last[trk_index(t)];
        pat_sig[trk_index(t)] = undo.pat;
        undo.pat = ps;
        pat_last[trk_index(t)] = undo.patn;
        undo.patn = pn;
        for (i = P_SLEN; i <= P_SGATE; i++) {
            int16_t v = t->p[i];
            t->p[i] = undo.p[i];
            undo.p[i] = v;
        }
        for (i = 0; i < NSTEP; i++) {
            step_t s = t->step[i];
            t->step[i] = undo.step[i];
            undo.step[i] = s;
        }
    }
    fm1_irq_on();
    if (undo.what & UNDO_SOUND) {                 /* FM6: the track's patch as it was (edited, a project's, a
                                                   * converted DIGITAL sound), not its PTCH's factory one */
        uint32_t tr = trk_index(t);
        uint8_t v[FP_SIZE + 1u], sl = fm6_slot[tr];
        memcpy(v, fm6_patch[tr], FP_SIZE);
        fm6_set_patch(tr, undo.fm6);
        fm6_slot[tr] = undo.fm6_slot;             /* (fm6_poll: the patch stays) */
        memcpy(undo.fm6, v, FP_SIZE);
        undo.fm6_slot = sl;
    }
    undo.keep = 0;                                /* the next load copies the track as it is now */
    if (t == TSEL)
        sync_reload = 1;
    b[1] = (char)('1' + trk_index(t));
    ui_say("UNDO/REDO ", b);
    ui.force = 1;
}

/* a 16-step pattern (PATTERNS[] format, user presets too) into steps 1..16, the rest empty, LEN 16 */
static void load_pat16(track_t *t, const uint8_t *note, const uint8_t *flags)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++) {
        step_t *s = &t->step[i];
        uint8_t n = i < 16u ? note[i] : 0, fl = i < 16u ? flags[i] : 0;
        s->note[0] = n;
        s->n = n ? 1 : 0;
        s->time = (fl & 4u) ? ST_TIE : n ? ST_NOTE : ST_REST;
        s->flags = n ? (fl & (SF_ACCENT | SF_SLIDE)) : 0;
        s->vel = n ? 96 : 0;
        s->hit = s->acc = 0;
        s->probability = 0;
        if (drum_track(t))                           /* a DRUM track: the lanes' notes as its grid */
            step_to_grid(s);
    }
    t->p[P_SLEN] = 16;
}

/* a 16-step drum grid (user presets of version 3: lane hits, their accents) into steps 1..16, the rest empty,
 * LEN 16 */
static void load_grid16(track_t *t, const uint8_t *hit, const uint8_t *acc)
{
    uint32_t i;
    for (i = 0; i < NSTEP; i++) {
        step_t *s = &t->step[i];
        step_clear(s);
        if (i < 16u && hit[i]) {
            s->time = ST_NOTE;
            s->hit = hit[i];
            s->acc = acc[i] & hit[i];
        }
    }
    t->p[P_SLEN] = 16;
}

static void track_defaults_steps(track_t *t)
{
    uint32_t i;
    motion_reset(t);
    for (i = 0; i < NSTEP; i++)
        step_clear(&t->step[i]);
}

/* SEQ > PATTERNS: the factory patterns (PATTERNS[], "01".."13"), then the used user presets that hold
 * one ("U07"): list index n. Loading one replaces the track's steps 1..16 (the rest cleared) and LEN;
 * a user preset's pattern brings its stored LEN (at most 16), DIV, SWING and GATE too. The notes are
 * loaded as they are: the patterns are written for the register of their kind of sound, PERC and
 * SLICE patterns are drum and slice numbers, and SCL TRANS / OCT transpose what plays */
static uint32_t pat_count(void) { return NPATTERNS + up_pat_count(); }

static void pat_label(uint32_t n, char *tag, char *name)   /* tag: 4 bytes ("01", "U07"), name: 13 */
{
    if (n < NPATTERNS) {
        tag[0] = (char)('0' + (n + 1u) / 10u);
        tag[1] = (char)('0' + (n + 1u) % 10u);
        tag[2] = 0;
        str_cpy(name, PATTERNS[n].name, 13);
    } else {
        uint32_t k = up_pat_nth(n - NPATTERNS);
        up_slot_label(tag, k);
        up_name(k, name);
    }
}

static void pat_load(track_t *t, uint32_t n)
{
    load_begin(t, UNDO_PAT);
    if (n < NPATTERNS)
        load_pat16(t, PATTERNS[n].note, PATTERNS[n].flags);
    else
        up_pat_load(t, up_pat_nth(n - NPATTERNS));
    pat_sig[trk_index(t)] = steps_sig(t);
    pat_last[trk_index(t)] = (uint8_t)(n + 1u);
    load_end(t);
    ui.force = 1;
}

/* a pattern load would throw away steps of the user's (recorded, edited, from a project): ask first */
static int pat_needs_confirm(const track_t *t)
{
    return !seq_is_empty(t) && steps_sig(t) != pat_sig[trk_index(t)];
}

static uint32_t pat_pick(void)                   /* ui.ppick inside the list (user presets may be gone) */
{
    uint32_t n = pat_count();
    return ui.ppick < n ? ui.ppick : n - 1u;
}

/* SEQ > PATTERNS LOAD: pattern n into track t, "LOADED 03 MELODY" */
static void pat_load_ui(track_t *t, uint32_t n)
{
    char tag[4], nm[13], b[20];
    pat_label(n, tag, nm);
    pat_load(t, n);
    str_cpy(b, tag, sizeof b);
    str_cpy(b + str_len(b), " ", 2);
    str_cpy(b + str_len(b), nm, sizeof b - str_len(b));
    ui_say("LOADED ", b);
}

/* the track's settings, not the sound's: what a sound load (factory or user preset, an engine jump,
 * TOOLS INIT) leaves alone. The mix (LEVEL, PAN, MUTE: the TRACKS faders), the arpeggiator (ARP, ARP 2),
 * the scale and key map (SCL), the pattern parameters (LEN, DIV, SWING, GATE) and the SLICER insert,
 * which chops whatever the track plays in time with its sequencer */
static int param_kept(uint32_t i)
{
    return i == P_LEVEL || i == P_PAN || i == P_MUTE || (i >= P_AMODE && i <= P_SGATE) ||
           (i >= P_SLCR && i <= P_SLDEPTH) || i == P_CHRD || i == P_VOIC;
}

/* a retired preset kept as an alias, so stored preset numbers stay valid: SAMPLE 1, once TRANH, is PIANO
 * (tools/gen_samples.py SMP_SET_ORIG). It loads as the original; browsing skips it. -> the preset k stands for */
static uint32_t preset_orig(const engine_t *e, uint32_t k)
{
    return e->presets == SMP_PRESET_TABLE && k < SMP_NSETS ? SMP_SET_ORIG[k] : k;
}

/* the presets of engine e that browsing shows before preset k (k = npresets: all of them) */
static uint32_t preset_rank(const engine_t *e, uint32_t k)
{
    uint32_t i, n = 0;
    if (e->presets != SMP_PRESET_TABLE)
        return k;
    for (i = 0; i < k; i++)
        n += preset_orig(e, i) == i;
    return n;
}

#define preset_shown(e) (ENGINES[e]->npresets - (ENGINES[e]->presets == SMP_PRESET_TABLE ? SMP_NALIAS : 0u))

#if !FELUCCA_FM4
/* DIGITAL (engine 1, retired): t's sound = p, values as DIGITAL has them, converted to FM6 with a patch of its own
 * (fm4_convert.c). Every path that brings a DIGITAL sound into a track ends here: a track never keeps engine 1 */
static void fm4_apply(track_t *t, int16_t *p)
{
    uint8_t v[FP_SIZE + 1u];
    uint32_t pr = fm4_convert(p, v), tr = trk_index(t), f;
    fm6_set_patch(tr, v);
    fm6_slot[tr] = (uint8_t)p[P_E7];                  /* (fm6_poll: the patch stays the converted one) */
    f = motion_guard();                               /* the audio ISR sees the old sound or the new one */
    memcpy(t->p, p, sizeof t->p);
    t->eng_req = ENGI_FM6;
    t->preset = (uint8_t)pr;
    motion_unguard(f);
}
static void fm4_track(track_t *t)                     /* t holds a DIGITAL sound (engine 1): convert it */
{
    int16_t p[P_COUNT];
    memcpy(p, t->p, sizeof p);
    fm4_apply(t, p);
}
/* DIGITAL preset k (a stored preset number of engine 1: the editor's PRESET, SET G_ENGSEL, an old project's
 * power-on sound): its sound as a preset load sets it, converted. The sound only (not the steps, not param_kept) */
static void fm4_load_preset(track_t *t, uint32_t k)
{
    int16_t p[P_COUNT];
    uint32_t i;
    load_begin(t, UNDO_SOUND);
    panic_req |= (uint8_t)(1u << trk_index(t));
    t->user = 0;
    if (t == TSEL)
        sync_reload = 1;
    memcpy(p, t->p, sizeof p);
    for (i = 0; i < P_E0; i++)
        if (!param_kept(i))
            p[i] = TP[i].def;
    fm4_preset_values(p, k);
    fm4_apply(t, p);
    load_end(t);
}
#endif

/* preset pi of the engine the track asked for: the sound only (not the steps, not param_kept) */
static void apply_preset_to(track_t *t, uint32_t pi)
{
    const engine_t *e = ENGINES[t->eng_req % NENGINES];
    uint32_t i;
#if !FELUCCA_FM4
    if (t->eng_req % NENGINES == ENGI_DIGITAL) {
        fm4_load_preset(t, pi);
        return;
    }
#endif
    load_begin(t, UNDO_SOUND);
    panic_req |= (uint8_t)(1u << trk_index(t));       /* MONO/POLY may change: release what sounds */
    t->user = 0;
    if (t == TSEL)
        sync_reload = 1;
    if (!e->npresets) {
        load_end(t);
        return;
    }
    pi = preset_orig(e, pi % e->npresets);
    t->preset = (uint8_t)pi;
    for (i = 0; i < P_E0; i++)                        /* the rest of the sound to its defaults: a preset */
        if (!param_kept(i))                           /* sounds the same after any edit */
            t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = (int16_t)e->presets[pi].e[i];
    t->p[P_ATK] = e->presets[pi].env[0];
    t->p[P_DEC] = e->presets[pi].env[1];
    t->p[P_SUS] = e->presets[pi].env[2];
    t->p[P_REL] = e->presets[pi].env[3];
    t->p[P_ED_FLT] = e->presets[pi].fenv;
    t->p[P_VOICE] = e->presets[pi].mono ? V_LEGATO : V_POLY;   /* mono presets keep the legato feel */
    {   /* the sends */
        static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
        const preset_t *pr = &e->presets[pi];
        for (i = 0; i < 4u; i++)
            t->p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
    }
    fm6_track_loaded(t);                              /* FM6: the preset's patch (its PTCH) */
    load_end(t);
}

/* the engine's defaults and its first preset. With the audio IRQ off: the ISR sees the old engine with
 * its values or the new one with its own (voice.c engine_block), never one with the other's */
static void set_engine_of(track_t *t, uint32_t ei)
{
    const engine_t *e = ENGINES[ei % NENGINES];
    uint32_t i;
#if !FELUCCA_FM4
    if (ei % NENGINES == ENGI_DIGITAL) {             /* DIGITAL (retired): its first preset, as FM6 */
        fm4_load_preset(t, 0);
        return;
    }
#endif
    load_begin(t, UNDO_SOUND);
    fm1_irq_off();
    t->eng_req = (uint8_t)(ei % NENGINES);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e->edit[i].def;
    apply_preset_to(t, 0);
    fm1_irq_on();
    load_end(t);
}

static void apply_preset(uint32_t pi) { apply_preset_to(TSEL, pi); }
static void set_engine(uint32_t ei) { set_engine_of(TSEL, ei); }

static void track_defaults(track_t *t)
{
    uint32_t i;
    for (i = 0; i < P_E0; i++)
        t->p[i] = TP[i].def;
    track_defaults_steps(t);
}

/* switch engine (its defaults + first preset) and say so */
static void select_engine(uint32_t e)
{
    set_engine(e);
    ui_say("ENGINE ", ENGINES[TSEL->eng_req]->name);
    ui.force = 1;
}

/* the presets of every engine (in ENGINE_ORDER), then the used user presets, as one list (the PRESETS knob and the
 * PRESETS page browse it) */
static uint32_t preset_all_pos(uint32_t *total)          /* list index of the selected track's preset */
{
    uint32_t n = 0, cur = 0, e, r;
    for (r = 0; r < NENG_SHOWN; r++) {                  /* (engines.c ENGINE_ORDER) */
        const engine_t *en = ENGINES[e = eng_vis(r)];
        if (e == TSEL->eng_req)
            cur = n + preset_rank(en, preset_orig(en, TSEL->preset % (en->npresets ? en->npresets : 1u)));
        n += preset_shown(e);
    }
    if (user_of(TSEL) < UP_SLOTS)
        cur = n + up_rank(user_of(TSEL));
    *total = n + up_count();
    return cur;
}

/* list index n (< total) -> engine, *k its preset; NENGINES = user preset, *k its slot */
static uint32_t preset_all_at(uint32_t n, uint32_t *k)
{
    uint32_t e = 0, i, r;
    for (r = 0; r < NENG_SHOWN && n >= preset_shown(eng_vis(r)); r++)
        n -= preset_shown(eng_vis(r));
    if (r == NENG_SHOWN) {
        *k = up_nth(n);
        return NENGINES;
    }
    e = eng_vis(r);
    for (i = 0; preset_orig(ENGINES[e], i) != i || n--; i++)    /* the n-th shown preset */
        ;
    *k = i;
    return e;
}

static uint32_t preset_pos(uint32_t *total)
{
    uint32_t all, current = preset_all_pos(&all), n = 0, pos = 0xFFFFFFFFu;
    if (!favorites.filter) { *total = all; return current; }
    for (uint32_t i = 0; i < all; i++) {
        uint32_t k, e = preset_all_at(i, &k);
        if (!favorite_has(e, k)) continue;
        if (i == current) pos = n;
        n++;
    }
    *total = n;
    return pos == 0xFFFFFFFFu ? n : pos; /* current sound need not be a favorite */
}
static uint32_t preset_at(uint32_t n, uint32_t *k)
{
    uint32_t all;
    if (!favorites.filter) return preset_all_at(n, k);
    preset_all_pos(&all);
    for (uint32_t i = 0; i < all; i++) {
        uint32_t e = preset_all_at(i, k);
        if (favorite_has(e, *k) && !n--) return e;
    }
    *k = UP_SLOTS;
    return NENGINES;
}
static int preset_favorite(void)
{
    uint32_t k = user_of(TSEL);
    return favorite_has(k < UP_SLOTS ? NENGINES : TSEL->eng_req,
                                        k < UP_SLOTS ? k : TSEL->preset);
}
static void preset_mark(int on)
{
    uint32_t k = user_of(TSEL);
    if (favorite_set(k < UP_SLOTS ? NENGINES : TSEL->eng_req, k < UP_SLOTS ? k : TSEL->preset, on)) {
        ui.force = 1;
        settings_save();
    }
}

/* the pattern the selected track's sound suggests: its index in the SEQ > PATTERNS list, or -1. A factory
 * preset's PAT(n); a user preset that holds a pattern: that one ("U07") */
static int32_t preset_pat_hint(void)
{
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    uint32_t u = user_of(TSEL);
    if (u < UP_SLOTS)
        return up_has_pat(u) ? (int32_t)(NPATTERNS + up_pat_rank(u)) : -1;
    if (!e->npresets)
        return -1;
    u = e->presets[TSEL->preset % e->npresets].pat;
    return u && u <= NPATTERNS ? (int32_t)u - 1 : -1;
}

static void preset_hinted(void)                     /* after a sound load: SEQ > PATTERNS starts at the suggested pattern */
{
    int32_t h;
    if ((h = preset_pat_hint()) >= 0)
        ui.ppick = (uint8_t)h;
    ui.force = 1;
}

static void preset_go(uint32_t n)                    /* load list index n into the selected track (the sound only) */
{
    uint32_t k, e = preset_at(n, &k);
    if (e == NENGINES) {
        up_load(k);
    } else {
        if (e != TSEL->eng_req)
            select_engine(e);
        apply_preset(k);
    }
    preset_hinted();
}

/* the EDIT layer's KNOB 2 (ui_layer.c): the selected track's engine only: its factory presets, then the used user presets
 * made with it (slot order). List index of the current sound; *total the length */
static uint32_t eng_list_pos(uint32_t *total)
{
    uint32_t e = TSEL->eng_req % NENGINES, np = ENGINES[e]->npresets, u = user_of(TSEL), k, n = 0;
    uint32_t cur = np ? TSEL->preset % np : 0u;
    for (k = 0; k < UP_SLOTS; k++)
        if (up_used(k) && up_engine(k) == e) {
            if (k == u)
                cur = np + n;
            n++;
        }
    *total = np + n;
    return cur;
}

static void eng_list_step(int32_t direction)         /* the next / previous sound of the engine (wraps) */
{
    uint32_t total, cur = eng_list_pos(&total), e = TSEL->eng_req % NENGINES, np = ENGINES[e]->npresets, k, n;
    if (total < 2u)
        return;
    n = (cur + (direction > 0 ? 1u : total - 1u)) % total;
    if (n < np) {
        apply_preset(n);
    } else {
        n -= np;
        for (k = 0; k < UP_SLOTS; k++)
            if (up_used(k) && up_engine(k) == e && !n--) {
                up_load(k);
                break;
            }
    }
    preset_hinted();
}

static void preset_step(int32_t direction)
{
    uint32_t total, cur = preset_pos(&total);
    if (!total) { ui_message("NO FAVORITES"); return; }
    preset_go(cur >= total ? (direction > 0 ? 0 : total - 1) :
              (cur + (direction > 0 ? 1u : total - 1u)) % total);
}

/* Seven display rows. Favorites use a bounded window, not a repeating carousel.
 * Return total for an empty row; a non-favorite current sound shows the start. */
static uint32_t preset_visible(uint32_t cur, uint32_t total, uint32_t row)
{
    uint32_t first, last;
    if (!total || row >= 7u) return total;
    if (!favorites.filter)
        return (cur + total * 4u + row - 3u) % total;
    first = cur < total && cur > 3u ? cur - 3u : 0u;
    last = total > 7u ? total - 7u : 0u;
    if (first > last) first = last;
    return first + row < total ? first + row : total;
}

/* HOME: what KNOB k edits: the engine's four main parameters */
static const param_desc_t *home_param(uint32_t k, int16_t **vp)
{
    uint32_t id = ENGINES[TSEL->eng_req % NENGINES]->knob[k & 3u];
    *vp = &TSEL->p[id];
    return track_desc(TSEL, id);
}

/* select track i (KNOB 1 on TRACKS, the editor): its sound, pages and pattern from now on */
static void track_select(uint32_t i)
{
    if (i >= NTRK || i == song.sel)
        return;
    song.sel = (uint8_t)i;
    ui.entry_open = 0;
    ui.hot_t = 0;
    ui.cursor = 0;
    ui.bank = 0;
    sync_reload = 1;
    ui.force = 1;
}

#include "ui_slice.c"                             /* EDIT > SLICES: SLICE's slices by hand (an action page too) */

/* ---------------------------------------------------- action pages --- */
/* Pages whose purpose is an action (SEQ > PATTERNS, SAVE > USER, PROJECT, TOOLS, EDIT > SLICES): the knobs pick,
 * OCT+ does it, OCT- cancels the picked action or goes HOME (ui_input.c). There OCT- / OCT+ do not
 * shift the octave */
static int go_id(uint32_t id) { return id == G_LOAD || id == G_SAVE || id == G_CLRSEQ || id == G_INITSND; }

static uint32_t act_cols(void)                   /* the columns that are actions, a bit each; 0 = not such a page */
{
    const page_t *pg = cur_page();
    uint32_t c, m = 0;
    if (ui.home)
        return 0;
    if (pg->graph == GR_MOTION) return 8u;
    if (pg->graph == GR_TOOLS) return 15u;
    if (pg->graph == GR_SONG)
        return 1u;                               /* PLAY / STOP (also the PLAY button) */
    if (pg->graph == GR_PATS)
        return 2u;                               /* LOAD */
    if (pg->graph == GR_USER)
        return 14u;                              /* LOAD ERASE SAVE */
    if (pg->graph == GR_SLICES)
        return slice_page_ok() ? 12u : 0u;       /* SPLIT JOIN (a SLICE track only) */
    if (pg->scope == SC_GLOBAL)
        for (c = 0; c < 4u; c++)
            if (go_id(pg->id[c]))
                m |= 1u << c;
    return m;
}

/* the action OCT+ does: its column + 1, 0 = none picked yet (PATTERNS has LOAD only) */
static uint32_t act_col(void)
{
    if (!ui.home && cur_page()->graph == GR_SONG) return 1u;
    return !ui.home && cur_page()->graph == GR_PATS ? 2u : ui.act;
}

static const char *act_name(uint32_t c)          /* column c's action (the footer hint) */
{
    static const char *const UP_GO[3] = {"LOAD", "ERASE", "SAVE"};
    uint32_t id = cur_page()->id[c & 3u];
    if (cur_page()->graph == GR_MOTION) return "CLEAR";
    if (cur_page()->graph == GR_TOOLS) {
        static const char *const actions[] = {"CLEAR", "INIT", "DELETE", "CLEAR"};
        return actions[c & 3u];
    }
    if (cur_page()->graph == GR_SONG)
        return song.playing || chain_busy() ? "STOP" : "PLAY";
    if (cur_page()->graph == GR_PATS)
        return "LOAD";
    if (cur_page()->graph == GR_USER)
        return UP_GO[(c + 2u) % 3u];
    if (cur_page()->graph == GR_SLICES)
        return c == 3u ? "JOIN" : "SPLIT";
    return id == G_CLRSEQ ? "CLEAR" : id == G_INITSND ? "INIT" : id == G_LOAD ? "LOAD" : "SAVE";
}

/* the picked action would do something now (OCT+ blinks): another pattern, a used slot, stopped for
 * a flash write, steps to clear */
static int act_ready(void)
{
    uint32_t c = act_col(), s = song.sel, id;
    if (!c--)
        return 0;
    if (cur_page()->graph == GR_MOTION) return !chain_busy() && motion_count(TSEL);
    if (cur_page()->graph == GR_TOOLS)                  /* one case per column: CLEAR PAT, INIT, DELETE ROW, CLEAR SONG */
        return !chain_busy() && (c == 0u ? !seq_is_empty(TSEL) || motion_count(TSEL) : c == 1u ? 1 :
                                 c == 2u ? ui.song_row < chain_config.count : chain_config.count != 0u);
    if (cur_page()->graph == GR_SONG)
        return song.playing || chain_busy() || chain_config.count;
    if (cur_page()->graph == GR_PATS)
        return pat_last[s] != pat_pick() + 1u || steps_sig(TSEL) != pat_sig[s];
    if (cur_page()->graph == GR_USER)
        return c == 3u ? !song.playing : up_used(ui.uslot) && (c == 1u || !song.playing);
#if FELUCCA_SLICE
    if (cur_page()->graph == GR_SLICES)
        return slice_act_ready(c);
#endif
    id = cur_page()->id[c & 3u];
    if (id == G_LOAD)
        return project_used((uint32_t)song.g[G_SLOT] - 1u);
    if (id == G_SAVE)
        return !song.playing;
    if (id == G_CLRSEQ)
        return !seq_is_empty(TSEL);
    return 1;
}
