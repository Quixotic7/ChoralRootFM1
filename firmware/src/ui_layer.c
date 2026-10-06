/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Felucca quick layers: while a page button is held, the keys and KNOB 1..4
 * are its shortcuts and its map shows over the page. One table (LAYERS) drives the gesture, the keys, the knobs,
 * the LEDs and the overlay:
 *   FX   HOLD  the performance effects (perform.c) and the track mutes while held; KNOB 1..4 its macros
 *   GLO  SET   black keys 1..4 (F#3 G#3 A#3 C#4) T1..T4 MUTE (latched; lit = sounding), F3..B3 SOLO T1..T4 while
 *              held (HOLD cells, a corner triangle), C4 UNMUTE ALL, F4 TAP tempo; KNOB 1..4 T1..T4 LEVEL;
 *              GLO + PLAY: from the top without stopping
 *   SCL  SET   any key: its note name is ROOT; KNOB 1..4 ROOT SCL CHRD VOIC (LY_SCL: the SCL page's first two,
 *              the CHORD page's two; QNT TRN stay on SCL); the LEDs show the root lit and the scale's notes blinking
 *   EDIT SET   the white keys from F3: the engines in PRESETS order (one key each, the NENG_SHOWN one can pick:
 *              engines.c eng_vis), the next white key INIT (LY_INIT: E5)
 *              (the dialog); KNOB 1 ENG, 2 No. (the engine's sounds), 3 FAV. Sound loads as on PRESETS: the steps
 *              stay, SAVE held undoes, the editor gets RELOAD; they apply while playing too
 * The gesture: let go before HOLD (the menu: 0.3 .. 0.6 s) with nothing else touched: a tap, the button's page.
 * Held past HOLD alone: the map (a peek), letting go does nothing. A key, a knob or a button meanwhile: a combo,
 * the map at once, no tap. Keys pressed with the button down are the layer's (seq.c keyboard_block): silent, no
 * MIDI, never recorded; keys held before stay notes. Only one layer at a time: a second layer button is ignored.
 * PLAY and REC work in every layer (GLO + PLAY: RESTART); SAVE, HOME, SEQ and the other page buttons are swallowed. In SET
 * layers OCT± do not shift the octave: OCT- (on release, not with OCT+) puts back what the layer changed since it
 * opened. A HOLD key's effect lasts until the key is let go, the map with it. No layer in the menu, a dialog,
 * NAME or the UPDATE MODE countdown. After a tap, until the layer has been opened once: "HOLD [GLO] QUICK"
 * (the seen bits are kept with the settings, favorites.c spare byte). */
enum { LK_HOLD, LK_SET };
typedef struct {
    uint8_t btn, kind, fam;            /* the button, HOLD / SET, the family whose first page KNOB 1..4 edit */
    const char *head;                  /* (FAM_HOME: the layer's own knobs) the header over the map */
    khint_t foot[3];                   /* the footer's key hints (a third with no word: none) */
} layer_t;
static const layer_t LAYERS[LAYER_N] = {
    {0, 0, 0, "", {{0, 0}, {0, 0}, {0, 0}}},
    {B_FX, LK_HOLD, FAM_HOME, "[FX] HOLD", {{KC_KEYS, "EFFECTS"}, {KC_K14, "MACROS"}, {0, 0}}},   /* (LET GO: the header's HOLD) */
    {B_GLO, LK_SET, FAM_HOME, "[GLO] SET", {{KC_PLAY, "RESTART"}, {KC_OCTDN, "UNDO"}, {KC_GLO, "DONE"}}},
    {B_SCL, LK_SET, FAM_SCL, "[SCL] SET", {{KC_KEYS, "ROOT"}, {KC_OCTDN, "UNDO"}, {KC_SCL, "DONE"}}},
    {B_EDIT, LK_SET, FAM_HOME, "[EDIT] SET", {{KC_KEYS, "ENGINE"}, {KC_OCTDN, "UNDO"}, {KC_EDIT, "DONE"}}},
};
static const uint8_t LY_KC[LAYER_N] = {0, KC_FX, KC_GLO, KC_SCL, KC_EDIT};
/* SCL's knobs: the key and its chord (cur_page() while the layer edits or draws them: page_over) */
static const page_t LY_SCL = {"SCL", FAM_SCL, SC_TRACK, GR_SCALE, {P_ROOT, P_SCALE, P_CHRD, P_VOIC}};
#define LY_OPEN 2u                     /* ui.ly_t0: the map opened (no tap any more) */
#define LY_COMBO 4u                    /* .. by a combo */
#define LY_DEAD 8u                     /* .. pressed where there is no layer: does nothing */
#define LY_INIT ((uint32_t)NENG_SHOWN) /* EDIT: the white key of INIT, the one after the engines (13: E5), its map cell */
typedef char ly_init_fits[LY_INIT < 16u ? 1 : -1];   /* (a white key: F3 .. G5) */
#define layer_seen (favorites.factory[15][31])   /* bit l: layer l opened once (a byte no engine uses) */

static struct {
    uint8_t l, trk, loaded, oct;       /* SET: the layer and the track of the snapshot; EDIT: a sound loaded since
                                        * it opened; OCT- / OCT+ pressed in a SET layer (bits) */
    int16_t v[9];                      /* the values when it opened: GLO mutes, levels, BPM; SCL ROOT..TRN, CHRD VOIC */
    uint32_t solo;                     /* GLO: the keys held that solo */
    uint32_t tap[4];                   /* GLO TAP: the last taps (fm1_ms) */
    uint8_t ntap;
} lys;

static int layer_allowed(void) { return !ui.menu && !ui.confirm && !ui.uboot && !name_on(); }
static uint32_t ly_bit(uint32_t l) { return 1u << panel.btn[LAYERS[l].btn]; }
static uint32_t ly_down(uint32_t l) { return l && (fm1_in.buttons & ly_bit(l)) != 0u; }
static uint32_t layer_bits(void)
{
    uint32_t l, m = 0;
    for (l = LAYER_FX; l < LAYER_N; l++)
        m |= ly_bit(l);
    return m;
}
static uint32_t layer_btn(void) { return LAYERS[ui.layer % LAYER_N].btn; }
static const char *layer_head(void) { return LAYERS[ui.layer % LAYER_N].head; }
static uint32_t layer_open(void) { return ui.ly && (ui.ly_t0 & LY_OPEN) && ly_down(ui.ly) && layer_allowed() ? ui.ly : 0u; }
static int layer_set_open(void) { return LAYERS[layer_open()].kind == LK_SET && layer_open(); }
static uint32_t layer_held(void) { return ui.ly && ly_down(ui.ly) && layer_allowed(); }

/* each pass, before the keys: what the ISR (seq.c keyboard_block) gives to a layer. Armed: its button; none
 * armed: any layer button not already down (one pressed meanwhile is armed below; one held over from before
 * is ignored, its keys stay notes); armed but dead (pressed under a dialog, a menu or NAME that has closed
 * since): nothing, its keys stay notes and FX starts no effect with no map shown; FX's effects only with FX */
static void layer_masks(void)
{
    uint32_t m = !layer_allowed() || (ui.ly && (ui.ly_t0 & LY_DEAD)) ? 0u
               : ui.ly ? ly_bit(ui.ly) : layer_bits() & ~fm1_in.buttons;
    kb_mask = m;
    perf_mask = m & ly_bit(LAYER_FX);
}

/* a layer button pressed (its edge) while none is armed: it is now, a dead one where no layer opens */
static void layer_arm(uint32_t pressed, uint32_t now)
{
    uint32_t l;
    if (ui.ly)
        return;
    for (l = LAYER_FX; l < LAYER_N; l++)
        if (pressed & ly_bit(l)) {
            ui.ly = (uint8_t)l;
            ui.ly_t0 = (now & ~15u) | 1u | (layer_allowed() ? 0u : LY_DEAD);
            return;
        }
}

/* the layer opened: once seen, no more hint; SET: what OCT- will put back */
static void layer_opened(uint32_t l)
{
    uint32_t k;
    if (!((layer_seen >> l) & 1u)) {
        layer_seen |= (uint8_t)(1u << l);
        settings_save();                                /* (deferred while playing) */
    }
    lys.l = (uint8_t)l;
    lys.trk = song.sel;
    lys.loaded = 0;
    for (k = 0; k < 4u; k++)
        if (l == LAYER_GLO) {
            lys.v[k] = trk[k].p[P_MUTE];
            lys.v[4u + k] = trk[k].p[P_LEVEL];
        } else {
            lys.v[k] = TSEL->p[P_ROOT + k];
            lys.v[4u + k] = k < 2u ? TSEL->p[P_CHRD + k] : 0;
        }
    lys.v[8] = song.g[G_BPM];
}

/* the armed button: 0, or the layer whose button was tapped (let go) */
static uint32_t layer_gesture(uint32_t now, uint32_t combo)
{
    uint32_t *t0 = &ui.ly_t0, l = ui.ly;
    if (!l)
        return 0;
    if (!layer_allowed())
        *t0 |= LY_DEAD;
    if (ly_down(l)) {
        if (!(*t0 & (LY_OPEN | LY_DEAD)) &&
            (combo || now - (*t0 & ~15u) >= (uint32_t)HOLD_MS[settings_hold % 4u] * 1000u * FM1_TICKS_PER_US)) {
            *t0 |= LY_OPEN | (combo ? LY_COMBO : 0u);
            layer_opened(l);
        }
        return 0;
    }
    if (l == LAYER_FX)
        perf_k[0] = perf_k[1] = perf_k[2] = perf_k[3] = 0;   /* the knob macros snap back */
    l = *t0 & (LY_OPEN | LY_DEAD) ? 0u : l;
    *t0 = 0;
    ui.ly = 0;
    return l;
}

/* the map shows while the button is held open, and after it while its keys are still held */
static void layer_show(void)
{
    uint32_t show = layer_allowed() ? (layer_open() ? layer_open() : kb_layer ? ui.layer : 0u) : 0u;
    if (show != ui.layer) {
        ui.layer = (uint8_t)show;
        ui.force = 1;
    }
}

/* a tap: the button's page; the hint until its layer has been opened once (unless the tap said something, or
 * opened NAME: EDIT on USER / PROJECT renames; never over a dialog or the menu) */
static void layer_tap(uint32_t l)
{
    uint8_t m = ui.msg_t;
    page_tap(LAYERS[l].btn);
    if (!((layer_seen >> l) & 1u) && ui.msg_t == m && !name_on() && !ui.confirm && !ui.menu) {
        ui_say("HOLD [", KC[LY_KC[l]].label);
        str_cpy(ui.msg + str_len(ui.msg), "] QUICK", sizeof ui.msg - str_len(ui.msg));
        ui.msg_t = 90;                                  /* ~1.5 s */
    }
}

/* EDIT: a sound load in the layer. The first one takes the undo copy as the track is now (when the layer
 * opened, unless KNOB 2 / 3 had loaded already), so OCT- and SAVE held go back to it */
static uint32_t snd_id(void) { return TSEL->eng_req | (uint32_t)TSEL->preset << 8 | (uint32_t)TSEL->user << 16; }
static void edit_load(uint32_t e, int32_t step)
{
    uint32_t id = snd_id();
    if (!lys.loaded)
        undo.keep = 0;
    if (e < NENGINES) {
        if (e == TSEL->eng_req % NENGINES)
            return;                                     /* its engine already: the sound stays */
        select_engine(e);
        preset_hinted();
    } else {
        eng_list_step(step);
    }
    lys.loaded |= snd_id() != id;
}

/* GLO TAP: from the third tap the tempo of the taps (the last 4); 2 s without one starts over. INT clock only */
static void glo_tap(void)
{
    uint32_t n = lys.ntap, i;
    if (song.g[G_CLOCK]) {
        ui_message("TAP: CLK IS EXT");
        return;
    }
    if (n && fm1_ms - lys.tap[n - 1u] > 2000u)
        n = 0;
    if (n == 4u) {
        for (i = 0; i < 3u; i++)
            lys.tap[i] = lys.tap[i + 1u];
        n = 3;
    }
    lys.tap[n++] = fm1_ms;
    lys.ntap = (uint8_t)n;
    if (n >= 3u && lys.tap[n - 1u] != lys.tap[0]) {
        song.g[G_BPM] = (int16_t)clamp((int32_t)(60000u * (n - 1u) / (lys.tap[n - 1u] - lys.tap[0])),
                                       GP[G_BPM].min, GP[G_BPM].max);
        ui.bpm_t = 40;
    }
}

/* a key pressed in layer l (k: 0 = F3 .. 26 = G5) */
static void layer_key(uint32_t l, uint32_t k)
{
    uint32_t p = key_place(k), i;
    if (l == LAYER_GLO) {
        if (key_black(k)) {
            if (p < NTRK)
                trk[p].p[P_MUTE] = (int16_t)!trk[p].p[P_MUTE];
        } else if (p < NTRK) {
            lys.solo |= 1u << k;                        /* (layer_masks: perf_solo while held) */
        } else if (p == 4u) {
            for (i = 0; i < NTRK; i++)
                trk[i].p[P_MUTE] = 0;
        } else if (p == 7u) {
            glo_tap();
        }
    } else if (l == LAYER_SCL) {
        TSEL->p[P_ROOT] = (int16_t)((k + 5u) % 12u);    /* the key's note name (F3 = F) */
    } else if (l == LAYER_EDIT && !key_black(k)) {
        if (p < NENG_SHOWN && p < LY_INIT)
            edit_load(eng_vis(p), 0);
        else if (p == LY_INIT && chain_busy())
            ui_message("STOP TO EDIT");
        else if (p == LY_INIT)
            confirm_open(CF_INIT_SOUND, song.sel);      /* (the dialog closes the layer) */
    }
}
/* each pass: the keys the layer got now; then GLO's solo: the tracks of its keys still held (perform.c) */
static void layer_keys(uint32_t keys)
{
    uint32_t k, l = layer_open() ? layer_open() : ui.layer, s = 0;
    for (k = 0; keys && k < 27u; k++)
        if ((keys >> k) & 1u)
            layer_key(l, k);
    lys.solo &= kb_layer;
    for (k = 0; k < 27u; k++)
        if ((lys.solo >> k) & 1u)
            s |= 1u << key_place(k);
    perf_solo = (uint8_t)s;
}

/* KNOB 1..4 in the open layer: its own (FX macros, GLO levels, EDIT's sound), else the four of its page */
static void layer_knob(uint32_t k, int32_t s)
{
    uint32_t l = layer_open();
    if (l == LAYER_FX) {                                /* perform.c perf_k, not recorded */
        perf_k[k] = (int8_t)clamp(perf_k[k] + s, k ? 0 : -100, 100);
    } else if (l == LAYER_GLO) {                        /* T1..T4 LEVEL, recorded as on MIXER */
        int16_t *vp = &trk[k].p[P_LEVEL];
        *vp = (int16_t)clamp(*vp + s, TP[P_LEVEL].min, TP[P_LEVEL].max);
        motion_capture(&trk[k], P_LEVEL, *vp);
    } else if (l == LAYER_EDIT) {                       /* ENG, No., FAV */
        if (k == 0u)
            edit_load(eng_step(TSEL->eng_req, s), 0);
        else if (k == 1u)
            edit_load(NENGINES, s);
        else if (k == 2u)
            preset_mark(s > 0);
    } else if (l) {                                     /* the page's knobs, wherever the page is */
        uint8_t h = ui.home, pg = ui.page;
        ui.home = 0;
        ui.page = (uint8_t)page_first(LAYERS[l].fam);
        page_over = l == LAYER_SCL ? &LY_SCL : 0;
        edit_param(k, s);
        page_over = 0;
        ui.home = h;
        ui.page = pg;
    }
}

/* PLAY with GLO held open: RESTART (from the top, playing on); stopped: PLAY. 1 = done here */
static int layer_play(void)
{
    if (layer_open() != LAYER_GLO || chain_busy())
        return 0;
    if (song.g[G_CLOCK] && song.playing) {
        ui_message("RESTART: CLK IS EXT");
        return 1;
    }
    transport_req = song.playing ? 3u : 1u;
    return 1;
}

/* OCT- / OCT+ in a SET layer: no octave; OCT- let go puts back what the layer changed. Returns the taps left */
static uint32_t layer_oct(uint32_t pressed, uint32_t oct)
{
    uint32_t dn = panel.btn[B_OCTDN], up = panel.btn[B_OCTUP], k, mine;
    if (layer_set_open())
        lys.oct |= (uint8_t)(((pressed >> dn) & 1u) | ((pressed >> up) & 1u) << 1);
    mine = oct & lys.oct;
    if (mine & 1u) {                                    /* OCT-: back to when it opened */
        if (lys.l == LAYER_EDIT) {
            if (lys.loaded)
                undo_swap();                            /* (the copy from its first load) */
            lys.loaded = 0;
        } else if (lys.l == LAYER_GLO) {
            for (k = 0; k < NTRK; k++) {
                trk[k].p[P_MUTE] = lys.v[k];
                trk[k].p[P_LEVEL] = lys.v[4u + k];
            }
            song.g[G_BPM] = lys.v[8];
            ui_message("MIX PUT BACK");
        } else {
            for (k = 0; k < 4u; k++)
                trk[lys.trk % NTRK].p[P_ROOT + k] = lys.v[k];
            trk[lys.trk % NTRK].p[P_CHRD] = lys.v[4];
            trk[lys.trk % NTRK].p[P_VOIC] = lys.v[5];
            ui_message("SCALE PUT BACK");
        }
    }
    lys.oct &= (uint8_t)(((fm1_in.buttons >> dn) & 1u) | ((fm1_in.buttons >> up) & 1u) << 1);
    return oct & ~mine;
}

/* --------------------------------------------------------- the LEDs --- */
/* lit = in effect now (held, the value, a track sounding), slow blink = can be pressed, dark = nothing there */
static int glo_sounding(uint32_t t) { return !trk[t].p[P_MUTE] && (!perf_solo || ((perf_solo >> t) & 1u)); }
static uint32_t layer_leds(void)
{
    uint32_t k, m = 0, blink = ((fm1_ms / 250u) & 1u) == 0u, l = ui.layer, held = perf_held, ok = perf_avail();
    uint32_t mask = scale_mask(TSEL), root = (uint32_t)TSEL->p[P_ROOT] % 12u;
    for (k = 0; k < 27u; k++) {
        uint32_t p = key_place(k), b = (uint32_t)key_black(k), on = 0, e;
        if (l == LAYER_FX) {                            /* effects blink, held lit, a too-long REPEAT dark */
            e = perf_key(k);
            on = e < PF_N && ((ok >> e) & 1u) && (((held >> e) & 1u) | blink);
        } else if (l == LAYER_GLO) {
            on = b ? p < NTRK && glo_sounding(p) :
                 p < NTRK ? ((lys.solo >> k) & 1u) | blink : p == 4u ? blink : p == 7u && !song.g[G_CLOCK] && blink;
        } else if (l == LAYER_SCL) {                    /* the root lit, the scale's notes blink */
            e = (k + 5u + 12u - root) % 12u;
            on = e == 0u || (((mask >> e) & 1u) && blink);
        } else if (l == LAYER_EDIT && !b) {             /* the engine lit, the others and INIT blink */
            on = p < NENG_SHOWN && p < LY_INIT ? eng_vis(p) == TSEL->eng_req % NENGINES || blink :
                 p == LY_INIT && blink && !chain_busy();
        }
        m |= on << k;
    }
    return m;
}

/* ------------------------------------------------------ the overlay --- */
/* One template: the header names the button and the kind ("[GLO] SET"); the cards are KNOB 1..4; the panel the
 * map of the keys as cells (the key's note name, a Fukiai icon, a name; FX's effects: the icon alone, 24 px, the
 * REPEATs' division in the other corner); the footer the keycaps. A cell: RAISE
 * (can be pressed), the selection's fill (the value now, SET), the accent (held, HOLD), DIM (cannot now: pressing
 * it says why), KEY (a muted track, as the MUTE badge); HOLD cells in a SET layer: a corner triangle */
static const char *const PF_DIV[3] = {"1/8", "1/16", "1/32"};   /* the REPEATs (the other effects: their icon alone) */
static const uint8_t PF_ICON[PF_M1] = {ICON_X_REPEAT, ICON_X_REPEAT, ICON_X_REPEAT, ICON_X_REVERSE, ICON_CUTOFF,
    ICON_X_HPF, ICON_X_TSTOP, ICON_X_FREEZE, ICON_X_OCT_UP, ICON_X_OCT_DN};
static const char W_NOTE[16] = {'F', 'G', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'A', 'B', 'C', 'D', 'E', 'F', 'G'};
static const char B_NOTE[NTRK] = {'F', 'G', 'A', 'C'};  /* black keys 1..4: F# G# A# C# */
#define LC_X(c) (6 + 58 * (int32_t)(c))                 /* cell column c: 54 px wide, 4 px apart */
#define LC_W 54
#define LC_H 42                                          /* the big cells: rows at y 4 and 50 */
#define LF_X(c) (6 + 46 * (int32_t)(c))                 /* FX: the 10 effects, 5 a row (F3 .. C4, D4 .. A4), 42 px */
#define LF_W 42
#define LM_Y 96                                          /* the black keys' row (22 px) */
#define LM_H 22
enum { LS_OFF, LS_SEL, LS_HELD, LS_WAIT, LS_DIM, LS_MUTE };

static int32_t lc_w = LC_W;                              /* the cells' width (FX's effects: LF_W) */
static uint16_t lc_fill(uint32_t st, uint16_t *ink)
{
    *ink = st == LS_DIM ? T_DIM : T_THEME;
    if (st == LS_MUTE) {
        *ink = T_INK;
        return T_KEY;
    }
    if (st == LS_HELD) {
        *ink = T_BG;
        return T_ACCENT;
    }
    if (st == LS_SEL || st == LS_WAIT) {
        *ink = T_INK;
        return T_THEME;
    }
    return T_RAISE;
}
/* a cell at x, y, h px high: big (h > 30) the icon over the name (FX's effects, LF_W wide: the icon alone, 24 px,
 * under the note; a name: in the top right corner); a name: compact, the icon at the right (none: no icon); else a
 * black key's: two icons at the right. tri: a HOLD cell in a SET layer */
static void lcell(int32_t x, int32_t y, int32_t h, const char *note, uint32_t icon, uint32_t icon2, const char *name,
                  uint32_t st, int tri)
{
    uint16_t ink, fill = lc_fill(st, &ink);
    cv_rrect(x, y, lc_w, h, 4, fill, T_SURF);
    if (note)
        cv_text_on(x + 5, y + 3, &AF_S, note, fill == T_RAISE ? T_MID : ink, fill);
    if (h > 30 && lc_w == LF_W) {
        cv_icon_on(x + (lc_w - 24) / 2, y + h - 25, 24, icon, ink, fill);   /* (its ink: rows 3 .. 20 of 24) */
        if (name)
            cv_text_r(x + lc_w - 5, y + 3, &AF_S, name, ink, fill);
    } else if (h > 30) {
        cv_icon_on(x + (lc_w - 16) / 2, y + 4, 16, icon, ink, fill);
        cv_text_c(x + lc_w / 2, y + 23, &AF_S, name, ink, fill);
    } else if (name && h > 24) {
        cv_icon_on(note ? x + lc_w - 17 : x + 5, y + 4, 12, icon, ink, fill);
        cv_text_c(x + lc_w / 2, y + 15, &AF_S, name, ink, fill);
    } else if (name) {
        if (!note && icon < ICON_COUNT) {                  /* (the EDIT layer: the engine's icon instead of the key) */
            cv_icon_on(x + 3, y + (h - 12) / 2, 12, icon, ink, fill);
            cv_text_r(x + lc_w - 3, y + 5, &AF_S, name, ink, fill);
        } else {
            cv_text_r(x + lc_w - 5, y + 5, &AF_S, name, ink, fill);
        }
    } else {
        cv_icon_on(x + lc_w - 31, y + 5, 12, icon, ink, fill);
        cv_icon_on(x + lc_w - 17, y + 5, 12, icon2, ink, fill);
    }
    if (tri) {
        int32_t j;
        for (j = 0; j < 4; j++)
            cv_rect(x + lc_w - 7 + j, y + 2 + j, 4 - j, 1, ink);
    }
}
static void bnote(char *n, uint32_t t) { n[0] = B_NOTE[t]; n[1] = '#'; n[2] = 0; }

static void layer_fx(void)
{
    uint32_t held = perf_kill ? 0u : perf_held, act = perf_act, ok = perf_avail(), e;
    char n[3] = {0, 0, 0};
    lc_w = LF_W;
    for (e = 0; e < PF_M1; e++) {                       /* the effects of the white keys F3 .. A4, 5 a row */
        uint32_t st = !((ok >> e) & 1u) ? LS_DIM : !((held >> e) & 1u) ? LS_OFF : (act >> e) & 1u ? LS_HELD : LS_WAIT;
        n[0] = W_NOTE[e];
        lcell(LF_X(e % 5u), e < 5u ? 4 : 50, LC_H, n, PF_ICON[e], 0, e < 3u ? PF_DIV[e] : 0, st, 0);
    }
    lc_w = LC_W;
    for (e = 0; e < NTRK; e++) {                        /* the mutes of the black keys 1..4 */
        bnote(n, e);
        lcell(LC_X(e), LM_Y, LM_H, n, ICON_MUTE, trk_icon(e, 0), 0, (held >> (PF_M1 + e)) & 1u ? LS_MUTE : LS_OFF, 0);
    }
}
static void layer_glo(void)
{
    uint32_t e;
    char n[3] = {0, 0, 0};
    for (e = 0; e < NTRK; e++) {                        /* F3 .. B3: SOLO while held */
        n[0] = W_NOTE[e];
        lcell(LC_X(e), 4, LC_H, n, trk_icon(e, 0), 0, "SOLO", (perf_solo >> e) & 1u ? LS_HELD : LS_OFF, 1);
    }
    n[0] = 'C';
    lcell(LC_X(0), 50, LC_H, n, ICON_MUTE, 0, "ALL", LS_OFF, 0);   /* (unmute all) */
    n[0] = 'F';
    lcell(LC_X(3), 50, LC_H, n, ICON_TEMPO, 0, "TAP", song.g[G_CLOCK] ? LS_DIM : LS_OFF, 0);
    for (e = 0; e < NTRK; e++) {                        /* the black keys 1..4: MUTE, latched */
        bnote(n, e);
        lcell(LC_X(e), LM_Y, LM_H, n, ICON_MUTE, trk_icon(e, 0), 0, trk[e].p[P_MUTE] ? LS_MUTE : LS_OFF, 0);
    }
}
static void layer_scl(void)                             /* KNOB 2's scales, 4 x 4, the one now selected */
{
    uint32_t i, sc = (uint32_t)TSEL->p[P_SCALE];
    for (i = 0; i < 16u && i <= (uint32_t)TP[P_SCALE].max; i++) {
        int32_t x = LC_X(i % 4u), y = 4 + 29 * (int32_t)(i / 4u);
        uint16_t ink, fill = lc_fill(i == sc ? LS_SEL : LS_OFF, &ink);
        cv_rrect(x, y, LC_W, 25, 4, fill, T_SURF);
        cv_text_c(x + LC_W / 2, y + 7, &AF_S, TP[P_SCALE].names[i], ink, fill);
    }
}
static void layer_edit(void)                            /* the engines from F3, INIT next (LY_INIT), the sound under them */
{                                                        /* (cells show the engine's icon, not the key's note) */
    uint32_t n = NENG_SHOWN < LY_INIT ? NENG_SHOWN : LY_INIT, cells = n + 1u, i, h = cells > 12u ? 22u : 28u;
    for (i = 0; i < cells; i++) {
        int32_t x = LC_X(i % 4u), y = 4 + (int32_t)(h + 4u) * (int32_t)(i / 4u);
        const engine_t *en = ENGINES[eng_vis(i) % NENGINES];
        if (i < n)
            lcell(x, y, (int32_t)h, 0, engine_icon(en->name), 0, h > 24u ? en->name : eng_abbr(en->name),
                  eng_vis(i) == TSEL->eng_req % NENGINES ? LS_SEL : LS_OFF, 0);
        else
            lcell(x, y, (int32_t)h, 0, ICON_X_WARN, 0, "INIT", chain_busy() ? LS_DIM : LS_OFF, 0);
    }
    engine_sound_row(104);
}

static void layer_cards(uint32_t l)
{
    char val[12];
    const char *unit;
    uint32_t c;
    if (l == LAYER_FX) {                                /* FILTER CRUSH THROW DEPTH */
        int32_t m = perf_k[0];
        fmt_int(val, m < 0 ? -m : m);
        draw_column(0, "FILTER", m ? val : "OFF", m < 0 ? "LP" : m > 0 ? "HP" : "", m ? VAL(0u) : T_DIM, (m + 100) * 5,
                    ICON_CUTOFF);
        fmt_int(val, perf_k[1]);
        draw_column(1, "CRUSH", perf_k[1] ? val : "OFF", perf_k[1] ? "%" : "", perf_k[1] ? VAL(1u) : T_DIM,
                    perf_k[1] * 10, ICON_BITS);
        fmt_int(val, perf_k[2]);
        draw_column(2, "THROW", perf_k[2] ? val : "OFF", perf_k[2] ? "%" : "", perf_k[2] ? VAL(2u) : T_DIM,
                    perf_k[2] * 10, ICON_DELAY);
        if (perf_harm_on()) {                           /* OCT UP / DN playing: KNOB 4 is its shimmer (SHIMR) */
            fmt_int(val, perf_k[3]);
            draw_column(3, "SHIMR", perf_k[3] ? val : "OFF", perf_k[3] ? "%" : "", perf_k[3] ? VAL(3u) : T_DIM,
                        perf_k[3] * 10, ICON_FEEDBACK);
        } else {
            fmt_int(val, 100 - perf_k[3]);
            draw_column(3, "DEPTH", val, "%", VAL(3u), (100 - perf_k[3]) * 10, ICON_MIX);
        }
    } else if (l == LAYER_GLO) {                        /* T1..T4 LEVEL (the track's icon; muted: dim) */
        for (c = 0; c < NTRK; c++) {
            param_format(&TP[P_LEVEL], trk[c].p[P_LEVEL], val, &unit);
            draw_column(c, "LEVEL", val, unit, glo_sounding(c) ? VAL(c) : T_DIM, RATIO(&TP[P_LEVEL], trk[c].p[P_LEVEL]),
                        trk_icon(c, 1));
        }
    } else if (l == LAYER_EDIT) {
        engine_columns();
    } else {                                            /* the page's four (SCL: LY_SCL) */
        uint8_t h = ui.home, pg = ui.page;
        ui.home = 0;
        ui.page = (uint8_t)page_first(LAYERS[l].fam);
        page_over = l == LAYER_SCL ? &LY_SCL : 0;
        draw_columns();
        page_over = 0;
        ui.home = h;
        ui.page = pg;
    }
}

static void draw_layer(void)
{
    uint32_t l = ui.layer % LAYER_N, sig = l * 7919u + ux.gen * 977u;
    layer_cards(l);
    if (l == LAYER_FX)
        sig += (perf_kill ? 0u : perf_held) * 31u + perf_act * 131u + perf_avail() * 7u + (uint32_t)perf_harm_on() * 3u;
    else if (l == LAYER_GLO)
        sig += perf_solo * 31u + (uint32_t)song.g[G_CLOCK] * 5u +
               (uint32_t)(trk[0].p[P_MUTE] | trk[1].p[P_MUTE] << 1 | trk[2].p[P_MUTE] << 2 | trk[3].p[P_MUTE] << 3) * 131u;
    else if (l == LAYER_SCL)
        sig += (uint32_t)TSEL->p[P_SCALE] * 31u;
    else
        sig += snd_id() * 31u + (uint32_t)preset_favorite() * 5u + (uint32_t)chain_busy() * 3u + up_gen * 101u;
    if (ui.force || sig != ui.layer_sig) {
        ui.layer_sig = sig;
        cv_begin(240, H_GRAPH, T_BG);
        cv_rrect(3, 0, 234, H_GRAPH, 5, T_SURF, T_BG);
        cv_bg = T_SURF;
        if (l == LAYER_FX)
            layer_fx();
        else if (l == LAYER_GLO)
            layer_glo();
        else if (l == LAYER_SCL)
            layer_scl();
        else
            layer_edit();
        cv_blit(0, Y_GRAPH);
    }
    if (ui.force) {                                     /* the footer: what the keys, knobs and buttons do */
        cv_begin(240, H_FOOT, T_BG);
        cv_key_row(8, 232, 9, LAYERS[l].foot, LAYERS[l].foot[2].act ? 3u : 2u, 7u, T_BG);
        cv_blit(0, Y_FOOT);
        ui.foot_sig = 0;
    }
}
