/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca UI input: LEDs, knobs and buttons, SEQ step entry, panel setup. */
#include "ui_name.c"                                    /* NAME: naming user presets and projects */
/* ----------------------------------------------------------- LEDs --- */
/* The LED picture is built off-line and copied one byte per column: clearing
 * and relighting would let the 10 kHz scan catch the dark gap and flicker. */
static uint8_t led_pos[41];                        /* (col << 3) | row bit, 0xFF = none */

static void led_pos_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    led_pos[id] = (uint8_t)((p << 3) | r);
    }
}

static void led_put(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}

/* PLAY's second, green LED: not in the key matrix (no key there), found on the hardware at column 8, row PA9 (bit 1) */
#define LED_PLAY_GREEN ((8u << 3) | 1u)
static void led_clear(uint8_t *nl, uint32_t id)
{
    uint8_t q = led_pos[id];
    if (q != 0xFF)
        nl[q >> 3] &= (uint8_t)~(1u << (q & 7u));
}

static const uint8_t FAM_BTN[FAM_COUNT] = {B_HOME, B_ENV, B_LFO, B_FX, B_SCL, B_EDIT, B_GLO, B_SAVE,
                                           B_ARP, B_SEQ, B_GLO};   /* GLO: mixer + global settings; REC is transport */

static uint32_t cur_fam(void) { return ui.home ? FAM_HOME : cur_page()->fam; }

static int layer_set_open(void);                       /* (ui_layer.c) */
/* the OCT LEDs, bit 0 OCT-, bit 1 OCT+. In the dialogs, the menu and on action pages OCT- (back) is
 * lit and OCT+ blinks while it would do something; elsewhere they show the octave shift */
static uint32_t oct_leds(void)
{
    uint32_t blink = ((fm1_ms / 250u) & 1u) == 0u;
    if (name_on() && !ui.confirm && !ui.menu)           /* NAME: OCT- cancels, OCT+ (blinking) writes */
        return 1u | (blink ? 2u : 0u);
    if (layer_set_open())                               /* a SET layer: OCT- puts back (UNDO), OCT+ nothing */
        return 1u;
    if (ui.confirm || ui.menu || act_cols())
        return 1u | (blink && (ui.confirm || (ui.menu ? ui.menu == 1u : act_ready())) ? 2u : 0u);
    return (song.octave < 0 ? 1u : 0u) | (song.octave > 0 ? 2u : 0u);
}

/* the key LEDs on the grid, bit k = key k: the white keys show where the selected lane hits on the page
 * shown (its accents while ACC is held), the step playing inverted (a light walks over them); the black keys
 * the lane selected, ACC while held, the page keys while there is more than one page */
static uint32_t grid_leds(void)
{
    const track_t *t = TSEL;
    uint32_t k, m = 0, len = (uint32_t)t->p[P_SLEN], b = 1u << ui.lane, acc = (uint32_t)black_held(GK_ACC);
    uint32_t ph = song.playing && t->seq_idx < len && t->seq_idx / 16u == ui.bank ? t->seq_idx % 16u : 0xFFu;
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k), on;
        if (!key_black(k)) {
            uint32_t i = ui.bank * 16u + p;
            on = i < len && ((acc ? step_accents(&seq_steps(t)[i]) : step_lanes(&seq_steps(t)[i])) & b) != 0u;
            on ^= (uint32_t)(p == ph);
        } else {
            on = p < NLANE ? p == ui.lane : p == GK_ACC ? acc : len > 16u;
        }
        m |= on << k;
    }
    return m;
}

/* an ARP playing on any part flashes the ARP button on the beat, the bar's first beat longer: 1 lit, 0 dark,
 * 2 no ARP playing */
static uint32_t arp_led(void)
{
    uint32_t k, on = 0, b = beat_samples();
    for (k = 0; k < NPART; k++)
        on |= trk[k].p[P_AMODE] && trk[k].nheld;
    return !on ? 2u : beat_pos < (beat_n ? b / 6u : b / 2u);
}

/* the keys of the notes the selected track's sequencer and ARP sound now (#38), bit k = key k: where the keys
 * play that note (kb_map: the octave, TRN, QNT, an engine's own map), the lowest key that gives it (QNT SNAP
 * rounds the keys above down onto it); a note no key plays is not shown. A snapshot of the ISR's seq_notes /
 * arp_note: no state of its own, nothing to do while nothing sounds */
static uint32_t play_leds(void)
{
    const track_t *t = TSEL;
    uint8_t s[4 + NLANE + 1];
    uint32_t n = t->seq_n < 4u + NLANE ? t->seq_n : 4u + NLANE, i, k, note, used = 0, m = 0, hit;
    for (i = 0; i < n; i++)
        s[i] = t->seq_notes[i];
    if (t->arp_note)
        s[n++] = t->arp_note;
    for (k = 0; n && k < 27u; k++) {
        note = kb_map(t, k);
        for (i = 0, hit = 0; i < n; i++)
            if (s[i] == note && !((used >> i) & 1u)) {
                used |= 1u << i;
                hit = 1;
            }
        m |= hit << k;
    }
    return m;
}

/* 1: the keys show a map of their own (NAME, a layer's map: SCL's scale, FX; the DRUM grid, SLICES), lit or
 * dark; 0: the keys held and the notes playing, over the idle glow */
static int keys_own(void)
{
    return (name_on() && !ui.menu) || ui.layer || grid_on()
#if FELUCCA_SLICE
           || (!ui.menu && !name_on() && slice_page_on())
#endif
        ;
}

/* the key LEDs, bit k = key k: NAME's keys, the layer's map, the DRUM grid, else the keys held and the notes
 * the selected track's sequencer and ARP play (and on SLICES the keys of the selected slice) */
static uint32_t key_leds(void)
{
    uint32_t c = name_on() && !ui.menu ? name_leds() : ui.layer ? layer_leds() : grid_on() ? grid_leds() :
                 (fm1_in.notes & ~kb_layer) | play_leds();
#if FELUCCA_SLICE
    if (!ui.layer && !ui.menu && !name_on() && slice_page_on())
        c |= slice_leds();                              /* SLICES: and the keys of the selected slice */
#endif
    return c;
}

/* The LEDs: lit = active (the page's family, PLAY / REC running, the keys held or playing, a map's keys), the
 * blinking ones blink (the layer's button, the ARP beat, OCT+), every other button and key glows dim (#35: the
 * buttons of the black FM-1 can be found in the dark; hal/fm1_input.h fm1_led_dim, a short pulse each frame).
 * MENU > LEDS INV turns it around, as the stock firmware: the idle ones fully lit, the active ones dark, no glow
 * (a blink: lit / dark). The keys' own maps (keys_own) stay lit or dark in both, no glow: their dark keys read
 * as dark. Each picture is
 * built off-line and copied one byte per column, the glow first: an LED going from lit to dim never has a dark
 * frame */
static void ui_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0}, own[FM1_NCOL] = {0};
    uint32_t k, c;
    uint32_t fam = cur_fam();
    int keys_map = keys_own();
    static uint8_t ready;
    if (!ready) {
        led_pos_init();
        ready = 1;
    }
    if (!ui.layer || FAM_BTN[fam] != layer_btn())
        led_put(nl, panel.btn[FAM_BTN[fam]], 1);
    if ((k = arp_led()) != 2u && (!ui.layer || layer_btn() != B_ARP))
        led_put(nl, panel.btn[B_ARP], FAM_BTN[fam] == B_ARP ? !k : (int)k);   /* (on ARP's page: dark flashes) */
    if (ui.layer)                                       /* the layer's button blinks while its map is up */
        led_put(nl, panel.btn[layer_btn()], ((fm1_ms / 250u) & 1u) == 0u);
    led_put(nl, panel.btn[B_REC], song.rec != 0u);
    k = oct_leds();
    led_put(nl, panel.btn[B_OCTDN], (int)(k & 1u));
    led_put(nl, panel.btn[B_OCTUP], (int)(k >> 1));
    c = key_leds();
    for (k = 0; k < 27u; k++)
        led_put(keys_map ? own : nl, 14u + k, (int)((c >> k) & 1u));
    for (k = 0; k < NB; k++)
        led_put(nd, panel.btn[k], 1);
    for (k = 0; !keys_map && k < 27u; k++)
        led_put(nd, 14u + k, 1);
    if (song.playing)                                   /* playing: PLAY's green, its own LED dark in both modes */
        led_clear(nd, panel.btn[B_PLAY]);
    for (c = 0; c < FM1_NCOL; c++) {
        if (settings_leds == LEDS_INV) {                /* INV: the active ones dark, the rest lit */
            nl[c] = (uint8_t)(nd[c] & ~nl[c]);
            nd[c] = 0;
        }
        nl[c] |= own[c];
    }
    if (song.playing)
        nl[LED_PLAY_GREEN >> 3] |= (uint8_t)(1u << (LED_PLAY_GREEN & 7u));
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led_dim[c] = nd[c];
    for (c = 0; c < FM1_NCOL; c++)
        fm1_led[c] = nl[c];
}

/* ---------------------------------------------------------- input --- */
static int32_t accel(uint32_t role, int32_t s, int32_t range)
{
    /* Predictable hardware response: each decoded detent is one value step.
     * Fast turns retain their full signed detent count without time acceleration. */
    (void)role; (void)range;
    return s;
}

/* MIXER page: KNOB 1 LEVEL, 2 PAN, 3 REV send, 4 MUTE of the selected track (right = ON, left = OFF: the
 * track itself is ALGORITHM's, on every page). Pattern length stays on SEQ. */
static void tracks_edit(uint32_t slot, int32_t steps)
{
    track_t *t = TSEL;
    int16_t *vp;
    const param_desc_t *d;
    switch (slot) {
    case 3:
        t->p[P_MUTE] = (int16_t)(steps > 0);
        return;
    case 0:
        vp = &t->p[P_LEVEL];
        d = &TP[P_LEVEL];
        break;
    case 2:
        vp = &t->p[P_REV];
        d = &TP[P_REV];
        break;
    default:
        vp = &t->p[P_PAN];
        d = &TP[P_PAN];
        break;
    }
    *vp = (int16_t)clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max);
    motion_capture(t, (uint32_t)(vp - t->p), *vp);
}

/* REC tap on every page: arm / disarm live recording on the selected
 * track without navigating; arming while stopped starts the transport too. On STEP, keys then record live (at the
 * play head) instead of writing the cursor step */
static void rec_tap(void)
{
    uint8_t bit = (uint8_t)(1u << song.sel);
    if (!ui.home && cur_page()->graph == GR_SONG && !(song.rec & bit)) {
        ui_message("[SEQ] TO RECORD");
        return;
    }
    song.rec ^= bit;
    ui.force = 1;                                     /* also refresh the status on MENU / ABOUT */
    if ((song.rec & bit) && !song.playing)
        transport_req = 1;
    if ((song.rec & bit) && grid_on())
        ui_message("LANE KEYS RECORD");               /* (the white keys stay the steps) */
    else if ((song.rec & bit) && !ui.home && cur_page()->graph == GR_ROLL)
        ui_message("KEYS RECORD LIVE");               /* (seq_entry pauses while armed and playing) */
}

/* live recording into the selected track now: the STEP page's key entry pauses meanwhile */
static int live_rec_sel(void) { return ((song.rec >> song.sel) & 1u) && (song.playing || transport_req == 1u); }

/* the OCT- / OCT+ dialog (ui_draw.c draws it); trk: the track or the slot it is about */
static void confirm_open(uint32_t kind, uint32_t trk)
{
    ui.confirm = (uint8_t)kind;
    ui.confirm_trk = (uint8_t)trk;
    ui.act = 0;
    ui.force = 1;
}

/* the grid's page down (-1) / up (+1): the cursor to the same place on it (at most the last step) */
static void page_go(int32_t d)
{
    uint32_t len = (uint32_t)TSEL->p[P_SLEN], pages = (len + 15u) / 16u, b;
    if (pages < 2u)
        return;
    b = (ui.bank + pages + (uint32_t)d) % pages;
    cursor_set((int32_t)(b * 16u + ui.cursor % 16u < len ? b * 16u + ui.cursor % 16u : len - 1u));
}

/* the grid's knobs: 1 STEP (the cursor), 2 LANE, 3 HIT and 4 ACC of the lane at the cursor (right on, left off) */
static void grid_edit(uint32_t slot, int32_t steps)
{
    if (slot == 0u)
        cursor_set(ui.cursor + steps);
    else if (slot == 1u)
        ui.lane = (uint8_t)clamp((int32_t)ui.lane + (steps > 0 ? 1 : -1), 0, NLANE - 1);
    else if (slot == 2u)
        grid_hit(TSEL, ui.cursor, ui.lane, steps > 0);
    else
        grid_acc(TSEL, ui.cursor, ui.lane, steps > 0);
}

/* the keys on the grid (presses): a white key toggles the selected lane at its step of the page (its accent
 * while ACC is held) and puts the cursor there; a lane key selects the lane (seq.c plays it); the page keys */
static void grid_keys(uint32_t pressed)
{
    uint32_t k, len = (uint32_t)TSEL->p[P_SLEN];
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k);
        if (!((pressed >> k) & 1u))
            continue;
        if (!key_black(k)) {
            uint32_t i = ui.bank * 16u + p;
            if (chain_busy()) { ui_message("STOP TO EDIT"); continue; }
            if (i >= len)
                continue;                               /* past LEN: no step there */
            if (black_held(GK_ACC))
                grid_acc(TSEL, i, ui.lane, 2);
            else
                grid_hit(TSEL, i, ui.lane, 2);
            cursor_set((int32_t)i);
        } else if (p < NLANE) {
            ui.lane = (uint8_t)p;
        } else if (p != GK_ACC) {
            page_go(p == GK_PGUP ? 1 : -1);
        }
    }
}

static void step_edit(uint32_t slot, int32_t steps)
{
    step_t *st = &TSEL->step[ui.cursor];
    uint32_t i;
    if (drum_track(TSEL)) {
        grid_edit(slot, steps);
        return;
    }
    switch (slot) {
    case 0:                                               /* STEP: the cursor */
        cursor_set(ui.cursor + steps);
        break;
    case 1:                                               /* NOTE: transpose the step */
        if (!st->n) {
            st->note[0] = last_note;
            st->n = 1;
            st->time = ST_NOTE;
            break;
        }
        for (i = 0; i < st->n; i++)
            st->note[i] = (uint8_t)clamp(st->note[i] + steps, 1, 127);
        st->time = ST_NOTE;
        last_note = st->note[0];
        break;
    case 2:
        st->time = (uint8_t)clamp((int32_t)st->time + (steps > 0 ? 1 : -1), ST_NOTE, ST_REST);
        break;
    default: {                                            /* FLAG: - / ACC / SLD / A+S */
        uint32_t f = (st->flags & SF_ACCENT ? 1u : 0u) | (st->flags & SF_SLIDE ? 2u : 0u);
        f = (uint32_t)clamp((int32_t)f + (steps > 0 ? 1 : -1), 0, 3);
        st->flags = (uint8_t)((st->flags & ~(SF_ACCENT | SF_SLIDE)) | (f & 1u ? SF_ACCENT : 0u) | (f & 2u ? SF_SLIDE : 0u));
        break;
    }
    }
}

static void edit_param(uint32_t slot, int32_t steps)
{
    int16_t *vp;
    const page_t *pg = cur_page();
    const param_desc_t *d;
    int32_t v;
    if (pg->graph == GR_CHANCE) {
        if (slot == 0u) cursor_set(ui.cursor + steps);
        else if (slot == 1u) {
            if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
            step_t *st = &TSEL->step[ui.cursor];
            step_set_chance(st, (uint32_t)clamp((int32_t)step_chance(st) + steps, 0, 100));
        }
        return;
    }
    if (pg->graph == GR_MOTION) {
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        if (slot == 0u) motion_set_enabled(TSEL, steps > 0);
        else if (slot == 3u) ui.act = steps > 0 ? 4u : 0u;
        return;
    }
    if (pg->graph == GR_SONG) {
        if (slot == 0u) {
            ui.song_row = (uint8_t)clamp((int32_t)ui.song_row + steps, 0,
                chain_config.count < CHAIN_ROWS ? chain_config.count : CHAIN_ROWS - 1u);
            return;
        }
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        if (slot == 3u) return;               /* PLAY is a button; no duplicate row-count knob */
        if (ui.song_row >= chain_config.count) {
            chain_row_t *r = &chain_config.row[ui.song_row];
            r->slot = ui.song_row ? chain_config.row[ui.song_row - 1u].slot : 0u;
            r->repeat = 1;
            chain_config.count = ui.song_row + 1u;
            if (slot == 1u) return;
        }
        if (slot == 1u)
            chain_config.row[ui.song_row].slot = (uint8_t)clamp((int32_t)chain_config.row[ui.song_row].slot + steps, 0, 3);
        if (slot == 2u)
            chain_config.row[ui.song_row].repeat = (uint8_t)clamp((int32_t)chain_config.row[ui.song_row].repeat + steps, 1, 16);
        return;
    }
    if (chain_busy() && (pg->scope == SC_STEP || pg->graph == GR_STEPS ||
        0)) {
        ui_message("STOP TO EDIT"); return;
    }
    if (pg->scope == SC_STEP) {
        step_edit(slot, steps);
        return;
    }
    if (pg->scope == SC_TRK) {
        tracks_edit(slot, steps);
        return;
    }
    if (pg->graph == GR_BROWSE) {                         /* KNOB 1: one preset, KNOB 2: the next / previous engine */
        if (slot == 0u) {
            preset_step(steps);
        } else if (slot == 1u) {
            select_engine(eng_step(TSEL->eng_req, steps));
        } else if (slot == 2u) {
            preset_mark(steps > 0);
        } else if (slot == 3u && favorites.filter != (uint32_t)(steps > 0)) {
            favorites.filter = steps > 0;
            ui.force = 1;
            settings_save();
        }
        return;
    }
#if FELUCCA_SLICE
    if (pg->graph == GR_SLICES && slot < 2u) {           /* SLICES: KNOB 1 the marker, 2 moves it (ui_slice.c) */
        if (slice_page_ok())                              /* (the engine changed before ui_draw left the page) */
            slice_knob(slot, steps);
        return;
    }
#endif
    if ((act_cols() >> slot) & 1u) {                      /* an action's knob picks it (right) or drops it (left); */
        if (pg->graph != GR_PATS)                         /* OCT+ does it (act_do) */
            ui.act = steps > 0 ? (uint8_t)(slot + 1u) : ui.act == slot + 1u ? 0u : ui.act;
        return;
    }
    if (pg->graph == GR_USER) {                           /* KNOB 1 the slot */
        if (slot == 0u)
            ui.uslot = (uint8_t)clamp((int32_t)ui.uslot + steps, 0, UP_SLOTS - 1);
        return;
    }
    if (pg->graph == GR_PATS) {                          /* KNOB 1 the pattern */
        if (slot == 0u)
            ui.ppick = (uint8_t)clamp((int32_t)pat_pick() + steps, 0, (int32_t)pat_count() - 1);
        return;
    }
    if (pg->graph == GR_MOD && slot == 0u) {             /* MOD: KNOB 1 the slot, 2..4 its SRC DST AMT */
        mod_ui_slot = (uint8_t)clamp((int32_t)mod_ui_slot + (steps > 0 ? 1 : -1), 0, 3);
        return;
    }
    d = page_desc(pg, slot, &vp);
    if (!d || !vp || d->max == d->min)
        return;
    v = enum_step(d, *vp, clamp(*vp + accel(EN_K1 + slot, steps, d->max - d->min), d->min, d->max));
    *vp = (int16_t)v;
    if (pg->scope != SC_GLOBAL) motion_capture(TSEL, (uint32_t)(vp - TSEL->p), *vp);
}

/* OCT+ on an action page: the picked action. A load stays picked (browse and load again); the others
 * are dropped once done. Flash writes only while stopped; over the user's data: the dialog */
static void act_do(void)
{
    uint32_t c = act_col(), id, k = (uint32_t)song.g[G_SLOT] - 1u;
    if (!c--)
        return;
    if (cur_page()->graph == GR_MOTION) {
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        confirm_open(CF_CLEAR_MOTION, song.sel);
        return;
    }
    if (cur_page()->graph == GR_TOOLS) {
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        if (!act_ready()) {                               /* nothing there to clear or delete */
            ui_message(c == 2u ? "NOTHING TO DELETE" : "NOTHING TO CLEAR");
            return;
        }
        confirm_open(c == 0u ? CF_CLEAR_SEQ : c == 1u ? CF_INIT_SOUND :
                     c == 2u ? CF_DEL_ROW : CF_CLEAR_SONG, c == 2u ? ui.song_row : song.sel);
        return;
    }
    if (cur_page()->graph == GR_SONG) {
        if (song.playing || chain_busy()) transport_req = 2;
        else chain_play_ui();
        return;
    }
    if (cur_page()->graph == GR_PATS) {
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        if (pat_needs_confirm(TSEL))                      /* the user's steps: the dialog */
            confirm_open(CF_LOAD_PAT, song.sel);
        else                                              /* empty, or a pattern loaded and untouched */
            pat_load_ui(TSEL, pat_pick());
        return;
    }
#if FELUCCA_SLICE
    if (cur_page()->graph == GR_SLICES) {                 /* SPLIT / JOIN: stays picked (split again, join again) */
        if (slice_page_ok())
            slice_act(c);
        return;
    }
#endif
    if (cur_page()->graph == GR_USER) {                   /* 1 LOAD, 2 ERASE, 3 SAVE (the NAME screen first) */
        if (c > 1u)
            ui.act = 0;
        if (c == 2u && up_used(ui.uslot) && !transport_busy())
            confirm_open(CF_ERASE_USER, ui.uslot);        /* ERASE: the dialog first */
        else if (c != 3u)
            up_ui(c - 1u, ui.uslot);
        else if (transport_busy())
            ui_message("STOP TO SAVE");
        else if (up_used(ui.uslot))
            confirm_open(CF_OVR_USER, ui.uslot);
        else
            name_open(NK_USER_SAVE, ui.uslot);
        return;
    }
    id = cur_page()->id[c & 3u];
    if (id != G_LOAD)
        ui.act = 0;
    switch (id) {
    case G_LOAD:
        project_load(k);
        break;
    case G_SAVE:                                          /* the NAME screen writes it */
        if (transport_busy())
            ui_message("STOP TO SAVE");
        else if (project_used(k))
            confirm_open(CF_OVR_PROJ, k);
        else
            name_open(NK_PROJ_SAVE, k);
        break;
    case G_CLRSEQ:
        if (chain_busy()) { ui_message("STOP TO EDIT"); break; }
        confirm_open(CF_CLEAR_SEQ, song.sel);
        break;
    default:                                              /* G_INITSND */
        set_engine(TSEL->eng_req);                        /* engine defaults + its first preset (the steps stay) */
        ui_message("SOUND INIT");
        ui.force = 1;
        break;
    }
}

/* OCT- / OCT+ where they answer (the dialogs, the menu, action pages): on release, and only a press
 * that began there; both down together (UPDATE MODE, main.c) is no tap. Bit 0 OCT-, bit 1 OCT+ */
static uint32_t oct_taps(uint32_t pressed, int here)
{
    static uint8_t down, chord;
    uint32_t dn = panel.btn[B_OCTDN], up = panel.btn[B_OCTUP];
    uint32_t now = ((fm1_in.buttons >> dn) & 1u) | ((fm1_in.buttons >> up) & 1u) << 1, tap;
    if (here)
        down |= (uint8_t)(((pressed >> dn) & 1u) | ((pressed >> up) & 1u) << 1);
    if (now == 3u)
        chord = 1;
    tap = down & ~now;
    down &= (uint8_t)now;
    if (chord) {
        tap = 0;
        chord = now != 0u;
    }
    return tap;
}

/* SEQ step entry, acid style: the keys pressed together (POLY: up to 4 notes, MONO:
 * the last one) become the cursor step; releasing all keys moves on. With CHRD on a key
 * writes what it sounds, as live recording does: POLY its chord, MONO the chord's root */
static void seq_entry(uint32_t pressed)
{
    track_t *t = TSEL;
    step_t *st = &t->step[ui.cursor];
    uint32_t k;
    for (k = 0; k < 27u; k++) {
        uint8_t ch[CHORD_MAX];
        int32_t r;
        uint16_t mask;
        uint32_t note, n, i, j;
        if (!((pressed >> k) & 1u))
            continue;
        note = kb_map(t, k);
        if (note == KB_SILENT)
            continue;
        if (!ui.entry_open) {
            ui.entry_open = 1;
            st->n = 0;
            st->time = ST_NOTE;
        }
        n = chord_make(t, note, ch, &r, &mask);        /* (CHRD OFF, a kit: the note alone; MONO: the root) */
        if (t->p[P_VOICE] && !ENGINES[t->engine]->oneshot) {   /* (drums: hits stack as a chord) */
            st->note[0] = ch[0];
            st->n = 1;
        } else {
            for (i = 0; i < n && st->n < 4u; i++) {
                for (j = 0; j < st->n && st->note[j] != ch[i]; j++)
                    ;
                if (j == st->n)
                    st->note[st->n++] = ch[i];
            }
        }
        last_note = (uint8_t)note;
    }
    if (ui.entry_open && !(fm1_in.notes & ~kb_layer))
        cursor_set(ui.cursor + 1);
}

/* HOME / REC / SAVE: tap on release, hold 0.7 s fires once. t0 = press time | 1,
 * bit 1 = fired (or swallowed: then the release is no tap either) */
enum { BT_NONE, BT_TAP, BT_HOLD };
static uint32_t btn_hold(uint32_t *t0, uint32_t label, uint32_t now, int hold_ok)
{
    uint32_t tap;
    if ((fm1_in.buttons >> panel.btn[label]) & 1u) {
        if (!*t0)
            *t0 = (now | 1u) & ~2u;
        else if (hold_ok && !(*t0 & 2u) && now - (*t0 & ~3u) > 700u * 1000u * FM1_TICKS_PER_US) {
            *t0 |= 2u;
            return BT_HOLD;
        }
        return BT_NONE;
    }
    tap = *t0 && !(*t0 & 2u);
    *t0 = 0;
    return tap ? BT_TAP : BT_NONE;
}

/* the quick layers: ui_layer.c (included after this file) */
static void layer_masks(void);
static void layer_arm(uint32_t pressed, uint32_t now);
static uint32_t layer_held(void);
static uint32_t layer_gesture(uint32_t now, uint32_t combo);
static void layer_show(void);
static void layer_tap(uint32_t l);
static void layer_keys(uint32_t keys);
static void layer_knob(uint32_t k, int32_t s);
static int layer_play(void);
static int layer_set_open(void);
static uint32_t layer_oct(uint32_t pressed, uint32_t oct);
static int layer_allowed(void);
static uint32_t ly_bit(uint32_t l);

/* a page button let go (they act on release; a layer's own button: layer_gesture) */
static void page_tap(uint32_t b)
{
    uint32_t f;
    if (b == B_GLO) {
        open_global();                                  /* MIXER -> GLOBAL -> SYSTEM -> MIXER */
        return;
    }
    if (b == B_EDIT && song.seq_mode && !ui.home && cur_page()->graph == GR_ROLL) {   /* STEP: EDIT clears the step */
        if (chain_busy()) { ui_message("STOP TO EDIT"); return; }
        step_clear(&TSEL->step[ui.cursor]);
        cursor_set(ui.cursor + 1);
        ui_message("STEP CLEARED");
        return;
    }
    if (b == B_EDIT && !ui.home && (cur_page()->graph == GR_USER || cur_page()->graph == GR_SLOTS)) {
        name_rename();                                  /* SAVE > USER / PROJECT: EDIT renames the slot */
        return;
    }
    for (f = FAM_HOME + 1u; f < FAM_COUNT; f++)
        if (FAM_BTN[f] == b) {
            open_family(f);
            return;
        }
}

/* messages of things that happened elsewhere (a load, the editor, MIDI in): after this frame's own */
static void ui_notices(void)
{
    static uint32_t midi_t, midi_last;
    if (motion_full) { motion_full = 0; ui_message("MOTION FULL"); }
    if (midi_hint) {                                    /* MIDI notes into a track that is not selected */
        uint32_t h = midi_hint;
        midi_hint = 0;
        if (!ui.msg_t && (h != midi_last || fm1_ms - midi_t > 4000u)) {
            char b[4] = {'T', (char)('0' + h), 0, 0};
            ui_say("MIDI IN -> ", b);
            midi_last = h;
            midi_t = fm1_ms;
        }
    }
}

static void ui_input(void)
{
    uint32_t pressed = fm1_input_edges(0), notes = fm1_input_note_edges(), now = fm1_ticks(), id, b, k;
    uint32_t home = btn_hold(&ui.home_t0, B_HOME, now, 1);
    uint32_t rec = btn_hold(&ui.rec_t0, B_REC, now, 0);
    uint32_t seq = btn_hold(&ui.seq_t0, B_SEQ, now, !ui.menu && !ui.confirm);
    uint32_t save = btn_hold(&ui.save_t0, B_SAVE, now, !ui.menu && !ui.confirm);   /* held: UNDO (ui.c undo_swap) */
    uint32_t oct = oct_taps(pressed, ui.menu || ui.confirm || act_cols() || name_on() || layer_set_open());
    uint32_t lay, combo = 0, lytap, lkeys;
    int32_t s, ks[4] = {0, 0, 0, 0};
    fm6_poll();                                         /* FM6: PTCH turned -> its patch */
#if !FELUCCA_FM4
    for (k = 0; k < NTRK; k++)                          /* a DIGITAL sound any other way (the paths convert it */
        if (trk[k].eng_req == ENGI_DIGITAL)             /* already): FM6 (fm4_convert.c) */
            fm4_track(&trk[k]);
#endif
    oct = layer_oct(pressed, oct);                      /* (a SET layer's OCT-: put back) */
    layer_arm(pressed, now);
    layer_masks();                                      /* seq.c: keys pressed with a layer's button are its own */
    lay = layer_held();
    if (!layer_allowed())
        perf_kill = 1;                                  /* (effects off until their keys are let go) */
    else if (!kb_layer)
        perf_kill = 0;
    {   /* a key pressed with the button: a combo (its edge, or the ISR already took it); then not the grid's or a step's */
        static uint32_t kb_seen;
        lkeys = kb_layer & ~kb_seen;
        combo = lay && (notes || lkeys);
        kb_seen = kb_layer;
    }
    if (lay)
        lkeys |= notes & ~fm1_in.notes;                 /* (tapped and let go already) */
    notes &= ~kb_layer;
    if (lay) {                                          /* a layer's button held: keys, knobs and buttons are combos */
        combo |= (pressed & ~ly_bit(ui.ly)) != 0u;
        notes = 0;                                      /* (the keys are the layer's, not the grid's or a step's) */
        if (pressed & (1u << panel.btn[B_SAVE]))       /* no UNDO, no page */
            ui.save_t0 |= 2u;
        if (pressed & (1u << panel.btn[B_HOME]))       /* no menu, no HOME */
            ui.home_t0 |= 2u;
        if (pressed & (1u << panel.btn[B_SEQ]))
            ui.seq_t0 |= 2u;
        for (k = 0; k < 4u; k++)                        /* KNOB 1..4: the layer's (ui_layer.c layer_knob) */
            if ((ks[k] = panel_enc(EN_K1 + k)) != 0)
                combo = 1;
        panel_enc(EN_PRESET);                           /* (a stray turn would load another sound) */
        panel_enc(EN_ALGO);                             /* (another track: OCT- puts back the layer's track only) */
    }
    lytap = layer_gesture(now, combo);
    layer_show();
    layer_keys(lkeys);
    for (k = 0; k < 4u; k++)
        if (ks[k]) {
            layer_knob(k, ks[k]);
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
    song.grid = (uint8_t)keys_mode();                 /* (the menu, a dialog: the keys play again; NAME: silent) */
    if (home == BT_HOLD) {                              /* HOME held: open the menu, or leave it */
        if (ui.menu) {
            menu_close();
        } else {
            ui.menu = 1;
            ui.menu_sel = 0;
            ui.confirm = 0;                             /* (a clear dialog is cancelled, NAME too) */
            name_close();
            ui.force = 1;
            song.seq_mode = 0;
        }
    }
    if (ui.menu || ui.confirm || name_on()) {
        /* REC does nothing in the menu, a dialog or NAME (no transport start there) */
    } else if (chain_busy() && rec != BT_NONE) {
        ui_message("STOP TO RECORD");
    } else if (rec == BT_TAP) {
        rec_tap();
    }
    if (ui.menu) {                                      /* HOME / SAVE / REC taps do nothing here */
        if (ui.save_t0)
            ui.save_t0 |= 2u;
        ui.pg_down = 0;
        if (!ui.home_t0)
            menu_input(oct);
        return;
    }
    if (name_on() && !ui.confirm) {                     /* NAME: the keys type, KNOB 1 / 2, OCT+ / OCT- (ui_name.c); */
        if (ui.save_t0)                                 /* SAVE does nothing */
            ui.save_t0 |= 2u;
        if (((pressed >> panel.btn[B_PLAY]) & 1u) && (song.playing || chain_busy()))
            transport_req = 2;                          /* PLAY stops a transport started meanwhile (MIDI Start, the
                                                         * editor) so the name can be saved; it never starts one */
        ui.pg_down = 0;
        name_input(notes, oct);
        return;
    }
    if (save == BT_HOLD && chain_busy())
        ui_message("STOP TO UNDO");
    else if (save == BT_HOLD)                                /* SAVE held: undo the last sound load */
        undo_swap();
    else if (save == BT_TAP && !ui.confirm)             /* SAVE acts on release (a hold is the undo) */
        open_family(FAM_SAVE);
    if (ui.confirm) {                                   /* OCT- cancels, OCT+ does it; nothing else reacts */
        if (oct & 2u) {
            uint32_t kind = ui.confirm;
            ui.confirm = 0;
            ui.force = 1;
            if (kind == CF_OVR_PROJ) {                  /* overwrite: the NAME screen writes it */
                name_open(NK_PROJ_SAVE, ui.confirm_trk & 3u);
            } else if (kind == CF_OVR_USER) {
                name_open(NK_USER_SAVE, ui.confirm_trk);
            } else if (kind == CF_ERASE_USER) {
                up_ui(1u, ui.confirm_trk);
            } else if (kind == CF_LOAD_PAT) {
                pat_load_ui(&trk[ui.confirm_trk % NTRK], pat_pick());
                str_cpy(ui.msg2, "[SAVE] HOLD TO UNDO", sizeof ui.msg2);
            } else if (kind == CF_CLEAR_MOTION) {
                track_t *t = &trk[ui.confirm_trk % NTRK];
                if (!chain_busy()) { load_begin(t, UNDO_PAT); motion_clear(t); load_end(t); ui_message("MOTION CLEARED"); }
            } else if (kind == CF_DEL_ROW) {
                uint32_t r = ui.confirm_trk;
                if (!chain_busy() && r < chain_config.count) {
                    for (; r + 1u < chain_config.count; r++) chain_config.row[r] = chain_config.row[r + 1u];
                    chain_config.count--;
                    if (ui.song_row > chain_config.count) ui.song_row = chain_config.count;
                    ui_message("ROW DELETED");
                }
            } else if (kind == CF_CLEAR_SONG) {
                if (!chain_busy()) { chain_defaults(&chain_config); ui.song_row = 0; ui_message("SONG CLEARED"); }
            } else if (kind == CF_INIT_SOUND) {
                if (!chain_busy()) { set_engine(TSEL->eng_req); ui_message("SOUND INIT"); }
            } else {
                track_t *t = &trk[ui.confirm_trk % NTRK];
                load_begin(t, UNDO_PAT);
                track_defaults_steps(t);
                load_end(t);
                t->nheld = 0;                           /* and the latched arp chord */
                t->arp_phys = 0;
                if (kind == CF_CLEAR_TRK) {
                    char b[12] = "1 CLEARED";
                    b[0] = (char)('1' + ui.confirm_trk);
                    ui_say("TRACK ", b);
                } else {
                    ui_message("PATTERN CLEARED");
                }
            }
        } else if (oct & 1u) {
            ui.confirm = 0;
            ui.force = 1;
        }
        ui.pg_down = 0;
        enc_drop();
        return;
    }
    if (lytap)                                          /* a layer's button acts on release (held: the layer) */
        layer_tap(lytap);
    if (seq == BT_HOLD) {
        for (k = 0; k < NPAGES; k++) if (PAGES[k].graph == GR_SONG) break;
        ui.home = 0; ui.page = (uint8_t)k; page_entered();
    } else if (seq == BT_TAP) {
        open_family(FAM_SEQ);
    }
    if (home == BT_TAP)                                 /* HOME acts on release: a hold opens the menu */
        go_home();
    cursor_fix();                                       /* LEN may have changed (knob, editor, load) */
    for (id = 0; id < 14u; id++) {
        if (!((pressed >> id) & 1u))
            continue;
        b = panel_btn_of(id);
        switch (b) {
        case B_PLAY:
            if (layer_play())                           /* (GLO held: RESTART) */
                break;
            if (song.playing || chain_busy())
                transport_req = 2;
            else if (!ui.home && cur_page()->graph == GR_SONG)
                chain_play_ui();
            else
                transport_req = 1;
            break;
        case B_SEQ:
        case B_REC:                                     /* tap / hold: above */
        case B_SAVE:
        case B_FX:                                      /* the layers' buttons: ui_layer.c */
        case B_GLO:
        case B_SCL:
        case B_EDIT:
        case B_HOME:
            break;
        case B_OCTDN:
        case B_OCTUP: {
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if (act_cols() || layer_set_open())         /* action pages: enter / back (below); SET layers: OCT- */
                break;
            if ((fm1_in.buttons & both) == both)
                song.octave = 0;
            else
                song.octave += b == B_OCTDN ? (song.octave > -3 ? -1 : 0) : (song.octave < 3 ? 1 : 0);
            break;
        }
        default:                                        /* page buttons (GLO SCL ENV LFO EDIT ARP): when let go */
            if (!lay)                                   /* (with FX held: swallowed) */
                ui.pg_down |= (uint16_t)(1u << id);
            break;
        }
    }
    b = ui.pg_down & ~fm1_in.buttons;
    ui.pg_down &= (uint16_t)~b;
    for (id = 0; b; id++, b >>= 1)
        if (b & 1u)
            page_tap(panel_btn_of(id));
    if (act_cols() && (oct & 2u)) {                     /* action pages: OCT+ does the picked action, */
        act_do();
    } else if (act_cols() && (oct & 1u)) {              /* OCT- drops it, or (none picked) goes HOME */
        if (cur_page()->graph != GR_PATS && ui.act)
            ui.act = 0;
        else
            go_home();
    }
    song.grid = (uint8_t)keys_mode();                 /* (seq.c: the keys are the grid's) */
#if FELUCCA_SLICE
    if (notes && slice_page_on())                       /* SLICES: a key picks the slice it plays */
        slice_keys_pick(notes);
#endif
    if (song.grid) {
        grid_keys(notes);
    } else if (song.seq_mode && cur_page()->graph == GR_ROLL) {   /* STEP (not CHANCE: its knobs only) */
        if (live_rec_sel())                             /* armed and playing: the keys record live, */
            ui.entry_open = 0;                          /* not into the cursor step too */
        else if (!chain_busy())
            seq_entry(notes);
        else if (notes)
            ui_message("STOP TO EDIT");
    }

    if ((s = panel_enc(EN_PRESET)) != 0 && (ui.home || cur_page()->graph == GR_BROWSE)) {
        /* PRESETS browses the selected part's sounds (all engines, then user presets) on HOME and the
         * PRESETS page only (never the steps); elsewhere (TRACKS too, where one records) a stray turn
         * would throw away the sound being edited */
        preset_step(s);                                  /* past the factory ones: user presets */
    }
    if ((s = panel_enc(EN_ALGO)) != 0)             /* ALGORITHM: the selected track, on every page */
        track_select((uint32_t)clamp((int32_t)song.sel + (s > 0 ? 1 : -1), 0, NTRK - 1));
    if ((s = panel_enc(EN_SELECT)) != 0) {          /* SELECT knob = global tempo */
        song.g[G_BPM] = (int16_t)clamp(song.g[G_BPM] + accel(EN_SELECT, s, 200), GP[G_BPM].min, GP[G_BPM].max);
        ui.bpm_t = 40;                              /* the header's BPM lights up; no message over the header */
    }
    for (k = 0; k < 4u; k++) {
        const page_t *pg = cur_page();
        int16_t *hv;
        if ((s = panel_enc(EN_K1 + k)) == 0)
            continue;
        if (ui.home || pg->scope == SC_STEP || pg->scope == SC_TRK || page_desc(pg, k, &hv) ||
            ((pg->graph == GR_USER || pg->graph == GR_MOD || pg->graph == GR_PATS) && k == 0u)
            || pg->graph == GR_SONG || (pg->graph == GR_SLICES && k < 2u)) {   /* (not an empty column) */
            ui.hot_col = (uint8_t)k;
            ui.hot_t = 40;
        }
        if (ui.home) {
            int16_t *vp;
            const param_desc_t *d = home_param(k, &vp);
            *vp = (int16_t)enum_step(d, *vp, clamp(*vp + accel(EN_K1 + k, s, d->max - d->min), d->min, d->max));
            motion_capture(TSEL, (uint32_t)(vp - TSEL->p), *vp);
        } else {
            edit_param(k, s);
        }
    }
    ui_notices();
}

/* ---------------------------------------------------- panel setup --- */
/* 30 s without input: give up and keep the old table (a stuck key cannot hang the boot) */
#define SETUP_IDLE_MS 30000u
static void setup_title(void)
{
    lcd_fill(0, 0, 240, 240, T_BG);
    {   /* the title with its icon (the menu row's), centred together; M from y 8 as before */
        const char *t = "HARDWARE CALIBRATION";
        int32_t x = (240 - (16 + 6 + text_w(&AF_M, t))) / 2;
        cv_begin(240, 24, T_BG);
        cv_icon_on(x, 4, 16, ICON_X_DOCTOR, T_THEME, T_BG);
        cv_text(x + 22, 3, &AF_M, t, T_TEXT);
        cv_blit(0, 5);
    }
    draw_text_box(0, 32, 240, &AF_S, "TEACH EACH BUTTON AND KNOB", T_MID, 1);
    lcd_fill(16, 56, 208, 1, T_LINE);
}
static void setup_show(const char *what, const char *name)     /* "PRESS" / "TURN RIGHT", the control */
{
    draw_text_box(0, 80, 240, &AF_S, what, T_MID, 1);
    draw_text_box(0, 100, 240, &AF_L, name, T_THEME, 1);
}
static void panel_setup(void)
{
    uint32_t i, used = 0, t0 = fm1_ms;
    const panel_t old = panel;
    setup_title();
    while (fm1_in.buttons) {                             /* wait for OCT-/OCT+ release */
        fm1_wdt_feed();
        if (fm1_ms - t0 > SETUP_IDLE_MS)
            goto timeout;
    }
    fm1_input_edges(0);
    for (i = 0; i < NB; i++) {
        uint32_t p = 0, id;
        setup_show("PRESS", B_NAME[i]);
        t0 = fm1_ms;
        while (!(p & ~used)) {
            fm1_wdt_feed();
            p |= fm1_input_edges(0);
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
        }
        for (id = 0; id < 14u; id++)
            if (((p & ~used) >> id) & 1u)
                break;
        panel.btn[i] = (uint8_t)id;
        used |= 1u << id;
    }
    used = 0;
    for (i = 0; i < NE; i++) {
        uint32_t e;
        int32_t st = 0;
        setup_show("TURN RIGHT", E_NAME[i]);
        for (e = 0; e < 7u; e++)
            fm1_enc_take(e);
        t0 = fm1_ms;
        for (;;) {
            fm1_wdt_feed();
            if (fm1_ms - t0 > SETUP_IDLE_MS)
                goto timeout;
            for (e = 0; e < 7u; e++)
                if (!((used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                    break;
            if (e < 7u)
                break;
        }
        panel.enc[i] = (uint8_t)e;
        panel.dir[i] = (int8_t)(st > 0 ? 1 : -1);
        used |= 1u << e;
        fm1_delay_ms(300);
        fm1_enc_take(e);
    }
    panel.magic = PANEL_MAGIC;
    lcd_fill(0, 0, 240, 240, T_BG);
    ui.force = 1;
    return;
timeout:
    panel = old;
    lcd_fill(0, 0, 240, 240, T_BG);
    ui.force = 1;
    ui_message("SETUP CANCELLED");
}
