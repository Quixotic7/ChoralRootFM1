/* SPDX-License-Identifier: GPL-3.0-only */
/* cr_move.c: ChoralRoot's own Ableton Move interface, "ChoralRoot" (FM1VST docs/RFM1-TAKEOVER.md, capability tk1; the
 * design: the owner's approved "ChoralRoot takeover, proposal 2" in the Ableton Move Designer, layouts A..F, its
 * controls' notes, 2026-11-20). FM1VST's tools/rfm1/rfm1_cr_tk_test.py drives it through rfm1_run --tk.
 *
 * Only an .rfm1 built with tk1 (FM1_HOST_TK) has it: tools/emu/emu_fw.c includes this file at its end, after the whole
 * firmware, so it reaches the UI's and the engine's statics as cr_ui.c does. The FM-1 build never sees it (its image is
 * the same bytes): the hooks in the firmware's files are behind #ifdef FM1_HOST_TK (cr_ui.c cu_mods_phys,
 * cr_settings.c css_read / css_write, emu_fw.c's init and frame).
 *
 * It drives the SAME engine and state as the FM-1's panel: keys go in as cu_mroot roots (MIDI Roots' path: the chord
 * keys held, Key Mode, LOCK, LATCH, the SUS window apply), the chord types as the FM-1 layout's type keys (cu_tk_*:
 * MIN + MAJ = SUS), the extensions as chord keys (LOCK's rules, cu_mod_press's), the progressions as triggers
 * (CRE_PKEY), the memories as the engine's eight chord pads (cr_pad_*), the step sequencer as the loop editor
 * (cu_ledit_*, cr_loop_ed_*), the knobs through the UI's own setters (cs, cr_post). What the Move sets the FM-1 shows,
 * and the other way round.
 *
 *   Pads (bottom-left first, rows of 8): the top two rows' left half the chord keys (DIM MIN MAJ SUS / 6 m7 M7 9;
 *   Shift + one: LOCK), their right half the memories of the bank (tap: plays; a key held + tap: saved; Delete + tap:
 *   cleared; a step held + tap: the step gets it); the two bottom rows the layout: KEYS (one octave, C..C), SCALE (the
 *   key's degrees, Key Mode on), CHROMATIC (rows a 4th apart), PROG (the progression's chords 1..16; Shift + Left /
 *   Right: 17..32, 33..48), or ISO: all 32 pads notes a 4th apart (Shift held: the chord keys back, to LOCK one).
 *   Menu tapped: the previous layout; held: the picker (layouts, banks, progressions).
 *   The sound editor (Shift + step 9 / 10: the chord / bass sound): the memories' pads are its groups OSC FILT ENV LFO
 *   / MOD FX MIX and CHORD <-> BASS (a group tapped again: its next screen), the wheel its lanes, knobs 1-4 the lane,
 *   5-8 the next one (proposal 4, G).
 *   Steps: the loop slot's pattern, 16 a page (Left / Right: the page, up to 64 steps); tap: an empty step gets the
 *   last chord played, a filled one is cleared; held: keys / memories write it, the knobs edit it.
 *   Shift + a step (proposal 4, C; the FM-1's menu language): KEY LOCK LATCH PERF B.PERF FX BASS BEAT EDIT B.EDIT ..
 *   OPTIONS (16). Tap: on / off; held: its settings (the FM-1's layer, open until Back or the same step held again):
 *   the wheel turns its pages, knobs 1-4 the page's four. Loop held: steps 1..10 the slots, the knobs the looper's.
 *   Knobs: Voicing, Bass reg., Perf amount, FX send, Key, Scale, Transpose, Tempo (one layer: Shift does not change
 *   them); a step held: Note len, Chance, Velocity, Ratchet, Condition, Voicing, Transpose, Steps. Touched or turned:
 *   a full-screen popup, the name, then the value big.
 *   The wheel: the chord preset; its click: the engine instead (again: the preset); Shift: the bass sound (Shift +
 *   click: its engine). Play: the sequencer play / stop (Shift: restart); Rec: by the record mode (Shift: the next
 *   mode); Undo: the looper's undo; Loop: tap LATCH, held the slots; Copy: LOCK; Mute: the bass on / off; Sample: the
 *   chord sound's favourite mark; Delete: held, the clear modifier; tapped alone, PANIC; Capture: the global save;
 *   Back: closes a popup, else the Options, a settings layer, the editor.
 * Deterministic but for the time (fm1_ms): holds, popups and flashes are timed. */
#include "rfm1_move.h"
#include "cr_move_font.h"

#define CMV_HOLD_MS 350u                           /* a tap is shorter (Menu, a step) */
#define CMV_POP_MS 1000u                           /* a knob's popup after its last turn / its release */
enum { CMV_V_DYN, CMV_V_FIXED, CMV_V_SOFT, CMV_V_HARD, CMV_V_N };   /* the pads' velocity (RemoteFM14Move's) */
static const char *const CMV_VNAME[CMV_V_N] = {"Dynamic", "Fixed", "Soft", "Hard"};
#define CMV_RATE_TICKS 3                           /* Perf amount: encoder ticks a step */
#define CMV_MSG_MS 1200u                           /* a message (saved, refused, PANIC) */
#define CMV_FLASH_MS 300u                          /* a memory pad's white flash when it is saved */
#define CMV_NMEM 64u                               /* 8 banks of 8 */
#define CMV_TRIG0 100u                             /* the engine's trigger ids of the Prog layout's pads (cr_prog_key:
                                                    * apart from the keybed's 0..47) */
#define CMV_GID 0xFFFFFF00u                        /* the gesture id of a step written from the last chord */

enum { CMV_KEYS, CMV_SCALE, CMV_CHROM, CMV_PROG, CMV_ISO, CMV_ISOK, CMV_NLAYOUT };
static const char *const CMV_LAYOUT[CMV_NLAYOUT] = {"KEYS", "SCALE", "CHROM", "PROG", "ISO", "ISOK"};
static const char *const CMV_LAYOUT_SAY[CMV_NLAYOUT] = {"Keys", "Scale", "Chromatic", "Prog", "Isomorphic",
                                                         "Isomorphic, in key"};
#define cmv_iso() (cmv.layout == CMV_ISO || cmv.layout == CMV_ISOK)   /* all 32 pads notes */

/* the pad kinds a press started (its release ends what the press started, whatever the layout became) */
enum { PK_NONE, PK_ROOT, PK_PTRIG, PK_MEM, PK_MOD, PK_PICK, PK_SC };

typedef struct { uint8_t used, q, x, vel; int16_t root; int8_t voic; } cmv_mem_t;   /* a cr_chord_t + the voicing */

/* the knobs' jobs (a page of eight each: main, a step held, Loop held; a settings layer's, the editor's and the
 * Options' are K_LAY0 + the knob: the FM-1's own, named by its screen) */
enum { K_VOIC, K_PERF, K_RATE, K_DIR, K_BREG, K_KEY, K_SCALE, K_TRANS,
       K_SOUND, K_BSOUND, K_GATE, K_SWING, K_BLEVEL, K_VEL, K_HUM, K_TEMPO,
       K_S_LEN, K_S_CHANCE, K_S_VEL, K_S_RAT, K_S_COND, K_S_VOIC, K_S_TR, K_S_STEPS,
       K_FXSEND, K_L_LEN, K_L_RATE, K_L_QUANT, K_L_CIN, K_L_LEVEL, K_L_BEAT, K_L_MODE,
       K_LAY0, K_NKIND = K_LAY0 + 8 };
static const char *const CMV_KNAME[K_LAY0] = {
    "Voicing", "Perform", "Perf amount", "Direction", "Bass reg.", "Key", "Scale", "Transpose",
    "Sound", "Bass sound", "Gate", "Swing", "Bass level", "Velocity", "Humanize", "Tempo",
    "Note len", "Chance", "Velocity", "Ratchet", "Condition", "Voicing", "Transpose", "Steps",
    "FX send", "Length", "Rate", "Quantize", "Count-in", "Level", "Beat", "Rec mode"};
/* Shift + a step: the FM-1's buttons (proposal 4, C) */
enum { SC_KEY, SC_LOCK, SC_LATCH, SC_PERF, SC_BPERF, SC_FX, SC_BASS, SC_BEAT, SC_N };   /* Shift + the pads of columns 7-8 */
/* Shift + a step (proposal 6, their icons lit while Shift is held): */
enum { CMV_S_VEL, CMV_S_SET, CMV_S_SWG, CMV_S_QNT, CMV_S_RATE, CMV_S_STP, CMV_S_LOOP, CMV_S_EDIT, CMV_S_TAP,
       CMV_S_RECM, CMV_S_CIN, CMV_S_MET, CMV_S_N };
static const char *const CMV_SSAY[CMV_S_N] = {"Fixed velocity", "Settings", "Swing", "Quantize", "Loop rate", "Steps",
                                             "Loop settings", "Sound editor", "Tap tempo", "Record mode", "Count-in",
                                             "Metronome"};
static const char *const CMV_SC[SC_N] = {"KEY", "LOCK", "LATCH", "PERF", "B.PRF", "FX", "BASS", "METRO"};
static const char *const CMV_SC_SAY[SC_N] = {"Key", "Lock", "Latch", "Perform", "Bass perform", "FX", "Bass",
                                             "Metronome"};
/* the perform modes on the Perform knob: Off, then cr_ui.c CU_PERF's seven */
static const char *const CMV_PERF[8] = {"Off", "Strum", "Strum 2", "Slop", "Arp", "Arp 2", "Pattern", "Harp"};

/* the design's colours (make_cr_v2.py) */
#define C_VIOLET_D 0x36, 0x10, 0x47
#define C_ORANGE_D 0x47, 0x21, 0x00
#define C_VIOLET 196, 58, 255
#define C_ORANGE 255, 120, 0
#define C_KEY_D 0x51, 0x51, 0x51
#define C_YELLOW 0xFF, 0xD2, 0x28
#define C_WHITE 255, 255, 255
#define C_MEM 0x13, 0xAB, 0xBF
#define C_SEQ 40, 110, 255
#define C_HELD 255, 120, 0
#define C_PROG 150, 90, 255
#define C_BASS 255, 90, 30
#define C_GREEN 40, 220, 90
#define C_RED 255, 40, 40
#define C_CYAN 0x13, 0xAB, 0xBF

static struct {
    uint8_t inited, shown, speech;
    uint8_t layout, prev, bank;                    /* the bottom rows' layout, the one a Menu tap flips to; the bank */
    uint8_t key_was;                               /* SCALE switched Key Mode on: what it was (restored on leaving) */
    uint8_t scale_keyed;                           /* .. 1 while SCALE holds it on */
    uint8_t prog_page, page;                       /* the Prog layout's chords 1-16 / 17-32 / 33-48; the steps' page */
    int8_t prog_last;                              /* the Prog layout's pad last pressed (0..47), -1 none */
    cmv_mem_t mem[CMV_NMEM];
    uint8_t pads_dirty;                            /* the bank's eight to the engine's pads (cr_pad_set) */
    /* held */
    uint32_t pad_held;                             /* bit n: pad n */
    uint8_t pad_kind[32], pad_arg[32];
    uint8_t ext_held;                              /* bit cr_mod_t: the extension pads held */
    uint8_t tk_ref[3];                             /* the FM-1 type keys F# G# A# the type pads hold (SUS: G# + A#) */
    uint8_t type_pads;                             /* bit 0..3: DIM MIN MAJ SUS pads held */
    uint8_t mem_down;                              /* bit j: memory pad j of the bank sounds (cr_pad_down) */
    uint8_t keys_down;                             /* key pads held (roots, triggers): a memory tap saves */
    uint8_t shift, del, del_used, menu, menu_used, shift_used;
    uint32_t menu_t0;
    int8_t step;                                   /* the step held (0..63 of the loop), -1 none */
    uint8_t step_btn;                              /* .. its button (0..15) */
    uint8_t step_used, step_ed, step_ext;          /* something done in the hold; the editor opened by it; the loop
                                                    * made longer for it: its steps before + 1 (0: not) */
    uint8_t step_lock;                             /* .. the FM-1's layer then (cu.lock): the editor is the PLAY menu's,
                                                    * which cu_loop_frame keeps open only while it shows */
    uint16_t step_gen;                             /* .. crl.gen when it was last said (a write: said again) */
    uint32_t step_t0;
    uint8_t at_last;                               /* the pressure last sent (CRE_AT) */
    /* the screen's overlays */
    int8_t pop_kind;                               /* a knob's popup: K_*, -1 none */
    uint8_t pop_touch;                             /* bit k: knob k touched */
    uint32_t pop_until;
    char msg_t[20], msg_v[24];
    uint32_t msg_until;
    uint8_t saved_n;                               /* the memory saved (1..64), its popup */
    char saved_name[16];
    uint32_t saved_until;
    int8_t flash_pad;                              /* a memory pad flashing white (its pad 0..31), -1 none */
    uint32_t flash_until;
    char wheel_t[16], wheel_v[24];                 /* the wheel's popup (the sound, the engine, the favourite) */
    uint32_t wheel_until;
    uint8_t wheel_touch;
    int8_t wheel_k;                                /* the wheel's screen: 0 / 1 the chord preset / engine, 2 / 3 the bass's, -1 a message */
    /* the last chord played (the steps' quick entry): the engine's (cr.last_chord) unless a memory was played since */
    cr_chord_t lc_shadow;
    int8_t lc_mem;                                 /* the memory played last (0..63) while lc_shadow is current */
    /* the steps' map (cr_loop_ed_map of the page) */
    crl_edmap_t map;
    uint16_t map_gen;
    uint8_t map_page, map_ok;
    /* speech */
    char said_chord[24];
    uint32_t msg_seen;                             /* cu.msg.until last mirrored */
    /* the empty bar (the owner, 2026-11-21): a switch to an empty slot taken while the loop played keeps time, a bar
     * of 16 steps running on the loop's tempo clock (crl.rt never stops); what the slot gets then joins in phase */
    uint8_t ghost, was_play, loads_seen;
    uint32_t ghost_rt0;
    /* proposal 4 (the owner's notes, 2026-11-21) */
    uint8_t sc;                                    /* Shift + a step down: the step + 1 (0: none) */
    uint8_t sc_open;                               /* .. held past CMV_HOLD_MS: its settings opened */
    uint32_t sc_t0;
    uint8_t loop, loop_used;                       /* Loop held (the slots, the looper's knobs); something done */
    uint32_t loop_t0;
    uint8_t wheel_eng, wheel_beng;                 /* the wheel turns the chord engine / Shift + wheel the bass engine */
    /* proposal 5 */
    uint8_t set_open, set_edit, set_sel;           /* Shift + S2: the Settings list; a value being edited; the row */
    uint8_t vmode, vfixed;                         /* the pads' velocity: CMV_V_*; Fixed's level */
    uint8_t copy, copy_used, paste_armed;          /* Copy held, used in this hold; a paste waiting for a step tap */
    uint8_t del_last;                              /* Delete held: 1 the last press cut, 2 pasted (seq_ui.c's rule) */
    uint8_t step_prev;                             /* the step held has been previewed */
    uint64_t marks;                                /* the clipboard's steps (absolute), blinking: white copied, red cut */
    uint8_t mark_cut;
    cr_chord_t mine;                               /* proposal 6: the last chord the player's own keys played (a key or a
                                                    * memory held as it sounded): a step tapped gets it, not the loop's */
} cmv ={.step = -1, .pop_kind = -1, .flash_pad = -1, .prog_last = -1, .lc_mem = -1, .prev = CMV_SCALE, .vfixed = 100};

static uint32_t cmv_now(void) { return fm1_ms; }
static void cmv_say(const char *t)                /* (printable ASCII only: the record's rule) */
{
    char s[RFM1_TK_SAY_LEN];
    uint32_t i;
    if (!cmv.speech)
        return;
    for (i = 0; t[i] && i < sizeof s - 1u; i++)
        s[i] = (unsigned char)t[i] == 0xB7u ? ',' : (unsigned char)t[i] < 32u || (unsigned char)t[i] > 126u ? ' ' : t[i];
    s[i] = 0;
    rfm1_tk_say(s, 1);
}

/* ---------------------------------------------------------------- text --- */
static void cmv_cpy(char *d, const char *s, uint32_t n) { cu_cpy(d, s, n); }
static int cmv_same(const char *a, const char *b)
{
    while (*a && *a == *b)
        a++, b++;
    return *a == *b;
}
static void cmv_cat(char *d, const char *s, uint32_t n) { cu_cat(d, s, n); }
static void cmv_int(char *d, int32_t v, int plus, uint32_t n) { cu_int(d, v, plus, n); }
static void cmv_upper(char *s)
{
    for (; *s; s++)
        if (*s >= 'a' && *s <= 'z')
            *s = (char)(*s - 32);
}
/* a chord's name as the screen shows it: root, quality, superscript ("Dm7", "CM7 9", "Csus") */
static void cmv_chord_str(char *d, uint32_t n, const char *r, const char *q, const char *sup)
{
    cmv_cpy(d, r, n);
    cmv_cat(d, q, n);
    cmv_cat(d, sup, n);
}
/* a memory's name (its root as the pad plays it: + the KEY layer's Transpose, as cr_pad_down) */
static void cmv_mem_name(const cmv_mem_t *m, char *d, uint32_t n)
{
    char r[4], q[5], sup[CR_SUP_MAX];
    if (!m->used) {
        cmv_cpy(d, "-", n);
        return;
    }
    if (m->q == CR_Q_PROG) {                        /* a progression's chord: its notes now */
        uint8_t nt[CRPG_NOTES];
        uint32_t k = crp_chord(m->x & 15u, CR_PROG_KEY(m->root), nt), i;
        int32_t tr = CR_PROG_TR(m->root);
        for (i = 0; i < k; i++) {
            int32_t x = (int32_t)nt[i] + tr;
            while (x < 0)
                x += 12;
            while (x > 127)
                x -= 12;
            nt[i] = (uint8_t)x;
        }
        if (!k) {
            cmv_cpy(d, "-", n);
            return;
        }
        cr_chord_recognise(nt, (int)k, r, q, sup);
    } else
        cr_chord_name((uint8_t)((((int32_t)m->root + cs.transpose) % 12 + 12) % 12), m->q, m->x, r, q, sup);
    cmv_chord_str(d, n, r, q, sup);
}
/* a printed name in words for the screen reader ("Dm7" -> "D minor seven", "CM7 9" -> "C major seven nine") */
static void cmv_words(const char *s, char *d, uint32_t n)
{
    static const struct { const char *t, *w; } TOK[] = {
        {"maj7", "major seven"}, {"M7", "major seven"}, {"dim", "diminished"}, {"sus", "sus"}, {"add", "add"},
        {"JAZZ", "jazz"}, {"WTF", "W T F"}, {"13", "thirteen"}, {"11", "eleven"}, {"9", "nine"}, {"7", "seven"},
        {"6", "six"}, {"5", "five"}, {"4", "four"}, {"2", "two"}, {"m", "minor"}, {"M", "major"}, {"+", "augmented"},
        {"#", "sharp"}, {"b", "flat"}, {"/", "over"}, {"o", "diminished"}, {"-", "minus"}};
    uint32_t i;
    d[0] = 0;
    if (!*s)
        return;
    {                                              /* the root: a letter, then # / b */
        char r[8] = {s[0], 0};
        s++;
        if (*s == '#') {
            cmv_cat(r, " sharp", sizeof r + 0u);
            s++;
        } else if (*s == 'b') {
            cmv_cat(r, " flat", sizeof r + 0u);
            s++;
        }
        cmv_cpy(d, r, n);
    }
    while (*s) {
        if (*s == ' ') {
            s++;
            continue;
        }
        for (i = 0; i < sizeof TOK / sizeof TOK[0]; i++) {
            uint32_t L = 0;
            while (TOK[i].t[L] && s[L] == TOK[i].t[L])
                L++;
            if (!TOK[i].t[L]) {
                cmv_cat(d, " ", n);
                cmv_cat(d, TOK[i].w, n);
                s += L;
                break;
            }
        }
        if (i == sizeof TOK / sizeof TOK[0]) {     /* anything else: itself (a note letter after "/") */
            char c[3] = {' ', *s, 0};
            cmv_cat(d, c, n);
            s++;
        }
    }
}
/* the value as words ("+2" -> "plus 2") */
static void cmv_vwords(const char *s, char *d, uint32_t n)
{
    d[0] = 0;
    for (; *s; s++) {
        char c[2] = {*s, 0};
        if (*s == '+')
            cmv_cat(d, "plus ", n);
        else if (*s == '-' && s[1] >= '0' && s[1] <= '9')
            cmv_cat(d, "minus ", n);
        else if (*s == '%')
            cmv_cat(d, " percent", n);
        else
            cmv_cat(d, c, n);
    }
}

/* -------------------------------------------------------------- drawing --- */
static uint8_t *cmv_px;                            /* the shim's buffer while a frame is drawn */
static void px_set(int x, int y, int v)
{
    if (x < 0 || x >= RFM1_TK_W || y < 0 || y >= RFM1_TK_H)
        return;
    if (v)
        cmv_px[(y >> 3) * RFM1_TK_W + x] |= (uint8_t)(1u << (y & 7));
    else
        cmv_px[(y >> 3) * RFM1_TK_W + x] &= (uint8_t)~(1u << (y & 7));
}
static void px_rect(int x, int y, int w, int h, int v)
{
    int i, j;
    for (j = y; j < y + h; j++)
        for (i = x; i < x + w; i++)
            px_set(i, j, v);
}
static void px_box(int x, int y, int w, int h, int v, int dotted)
{
    int i;
    for (i = x; i < x + w; i++)
        if (!dotted || i % 2 == 0) {
            px_set(i, y, v);
            px_set(i, y + h - 1, v);
        }
    for (i = y; i < y + h; i++)
        if (!dotted || i % 2 == 0) {
            px_set(x, i, v);
            px_set(x + w - 1, i, v);
        }
}
static int px_tw(const char *s, int k)
{
    int n = 0;
    while (s[n])
        n++;
    return n ? n * 6 * k - k : 0;
}
static const uint8_t *px_glyph(char ch)
{
    unsigned c = (unsigned char)ch;
    if (c == 0xB0u)
        c = 'o';                                   /* (degree: the half-diminished's) */
    else if (c == 0xB7u)
        c = '-';                                   /* (the FM-1's middle dot) */
    if (c < 32u || c > 126u)
        c = '?';
    return cmv_font5x7[c - 32u];
}
static int px_text(int x, int y, const char *s, int k, int v)
{
    for (; *s; s++, x += 6 * k) {
        const uint8_t *g = px_glyph(*s);
        int col, row;
        for (col = 0; col < 5; col++)
            for (row = 0; row < 7; row++)
                if (g[col] >> row & 1)
                    px_rect(x + col * k, y + row * k, k, k, v);
    }
    return x;
}
static void px_ctext(int cx, int y, const char *s, int k, int v) { px_text(cx - px_tw(s, k) / 2, y, s, k, v); }
static int px_fit(const char *s, int k)            /* the largest scale <= k at which s fits the screen */
{
    while (k > 1 && px_tw(s, k) > 126)
        k--;
    return k;
}
static void px_header(const char *left, const char *right)
{
    px_rect(0, 0, 128, 16, 1);
    px_text(1, 1, left, 2, 0);
    if (right && *right)
        px_text(127 - px_tw(right, 2), 1, right, 2, 0);
}
static void px_lbl(int x, int y, const char *s) { px_text(x, y, s, 2, 1); }
static void px_rlbl(int y, const char *s) { px_text(127 - px_tw(s, 2), y, s, px_tw(s, 2) <= 127 ? 2 : 1, 1); }
static void px_clbl(int y, const char *s) { px_ctext(64, y, s, px_fit(s, 2), 1); }
static void px_tag(int x, int w, const char *s)    /* an inverted tag at the top: LK, on, Bass */
{
    px_rect(x, 0, w, 14, 1);
    px_text(x + (w - px_tw(s, 2)) / 2, 0, s, 2, 0);
}

/* big text (proposal 3's screens): ChoralRoot's bold, the FM-1's own (AF_CRB: Inter Tight 700, cr_gfx.c), scaled to a
 * cap height and cut to one bit (a pixel lit when its four samples average half covered) */
static uint32_t bt_ch(char ch)
{
    uint32_t c = (unsigned char)ch;
    if (glyph_at(&AF_CRB, c) >= 0)
        return c;
    return c == 0xB0u ? 'o' : c == 0xB7u ? '-' : c;
}
static int32_t bt_cap(const aag_t **gh)            /* the cap height of AF_CRB in px ('H'), its glyph */
{
    *gh = cr_glyph_of(&AF_CRB, 'H');
    return (*gh)->bh ? (*gh)->bh : 28;
}
static int bt_w(const char *s, int h)              /* s's advance at cap height h */
{
    const aag_t *gh;
    int32_t cap = bt_cap(&gh), pen = 0;
    uint32_t prev = 0;
    for (; *s; s++) {
        uint32_t c = bt_ch(*s);
        if (prev)
            pen += kern(&AF_CRB, prev, c);
        pen += cr_glyph_of(&AF_CRB, c)->adv;
        prev = c;
    }
    return (int)((pen * h + 8 * cap) / (16 * cap));
}
static int bt(int x, int y, const char *s, int h, int v)   /* the caps' top at y; returns the pen at the end */
{
    const aafont_t *f = &AF_CRB;
    const aag_t *gh;
    int32_t cap = bt_cap(&gh), pen = 0;
    uint32_t prev = 0;
    for (; *s; s++) {
        uint32_t c = bt_ch(*s);
        const aag_t *g;
        if (prev)
            pen += kern(f, prev, c);
        g = cr_glyph_of(f, c);
        if (g->bw && cr_decode(f, g)) {
            int32_t gx = x + (pen + 16 * g->bx) * h / (16 * cap), gy = y + ((int32_t)g->by - gh->by) * h / cap;
            int32_t dw = (g->bw * h + cap - 1) / cap, dh = (g->bh * h + cap - 1) / cap, i, j, a, b;
            for (j = 0; j < dh; j++)
                for (i = 0; i < dw; i++) {
                    uint32_t sum = 0;
                    for (b = 1; b < 4; b += 2)
                        for (a = 1; a < 4; a += 2) {
                            int32_t su = (4 * i + a) * cap / (4 * h), sv = (4 * j + b) * cap / (4 * h);
                            if (su < g->bw && sv < g->bh)
                                sum += cr_gbuf[sv * g->bw + su];
                        }
                    if (sum >= 30u)
                        px_set(gx + i, gy + j, v);
                }
        }
        pen += g->adv;
        prev = c;
    }
    return x + (int)(pen * h / (16 * cap));
}
static int bt_fit(const char *s, int h, int w)     /* the largest cap height <= h at which s fits w px */
{
    while (h > 8 && bt_w(s, h) > w)
        h--;
    return h;
}
static void bt_c(int y, const char *s, int h)      /* centred, shrunk to fit (kept on the same middle) */
{
    int k = bt_fit(s, h, 126);
    bt(64 - bt_w(s, k) / 2, y + (h - k) / 2, s, k, 1);
}
/* the FM-1's chord: the root big, the small part beside it at the foot, the extension raised (its width: chord_w) */
static int bt_chord_w(const char *r, const char *q, const char *sup, int h)
{
    int w = bt_w(r, h);
    if (q && *q)
        w += 1 + bt_w(q, h * 55 / 100);
    if (sup && *sup)
        w += 2 + bt_w(sup, h * 42 / 100);
    return w;
}
static void bt_chord(int x, int y, const char *r, const char *q, const char *sup, int h, int maxw)
{
    int e, h0 = h;
    while (h > 10 && bt_chord_w(r, q, sup, h) > maxw)
        h--;
    y += h0 - h;                                   /* (shrunk: the foot stays) */
    e = bt(x, y, r, h, 1);
    if (q && *q)
        e = bt(e + 1, y + h - h * 55 / 100, q, h * 55 / 100, 1);
    if (sup && *sup)
        bt(e + 2, y, sup, h * 42 / 100, 1);
}
static void px_dashes(uint32_t n, uint32_t cur, int y, int w, int gap)   /* which of n: a box each, the one filled */
{
    uint32_t i;
    int x;
    if (n < 2u || n > 24u)
        return;
    while (w > 2 && (int)n * w + ((int)n - 1) * gap > 124) {
        w--;
        if (gap > 2 && w < 8)
            gap--;
    }
    x = 64 - ((int)n * w + ((int)n - 1) * gap) / 2;
    for (i = 0; i < n; i++) {
        if (i == cur)
            px_rect(x + (int)i * (w + gap), y, w, 3, 1);
        else
            px_box(x + (int)i * (w + gap), y, w, 3, 1, 0);
    }
}

/* ---------------------------------------------------------------- pads --- */
static int cmv_pad(int r, int c) { return r * 8 + c; }   /* r 0 = the bottom row */
static int cmv_base(void) { return 60 + 12 * cu.octave; }   /* C of the Move's octave (the FM-1's octave, C4 at 0) */

/* the scale's degrees from the tonic (semitones), how many */
static uint32_t cmv_degrees(uint8_t deg[12])
{
    uint16_t bits = cr_scale_bits(cs.tonic, cs.scale);
    uint32_t i, n = 0;
    for (i = 0; i < 12u; i++)
        if ((bits >> ((cs.tonic + i) % 12u)) & 1u)
            deg[n++] = (uint8_t)i;
    if (!n)
        deg[n++] = 0;
    return n;
}
/* the note a bottom pad plays in the KEYS / SCALE / CHROM layouts, -1 none */
static int cmv_pad_note(int r, int c)
{
    static const int8_t WH[8] = {0, 2, 4, 5, 7, 9, 11, 12}, BL[8] = {-1, 1, 3, -1, 6, 8, 10, -1};
    switch (cmv.layout) {
    case CMV_KEYS:
        return r == 0 ? cmv_base() + WH[c] : BL[c] < 0 ? -1 : cmv_base() + BL[c];
    case CMV_SCALE: {
        uint8_t deg[12];
        uint32_t n = cmv_degrees(deg), k = (uint32_t)c + (r ? n : 0u);
        return cmv_base() + cs.tonic + deg[k % n] + 12 * (int)(k / n);
    }
    case CMV_CHROM:
    case CMV_ISO:                                  /* (all four rows: the Move's own isomorphic layout) */
        return cmv_base() + r * 5 + c;
    case CMV_ISOK: {                               /* in key: each row up to the scale note nearest a 4th above the
                                                    * tonic (7 notes: 3 degrees; pentatonics, blues: 2; Fable's rule) */
        uint8_t deg[12];
        uint32_t n = cmv_degrees(deg), up = 1, i, k;
        int best = 99;
        for (i = 1; i < n; i++) {
            int dd = (int)deg[i] - 5;
            dd = dd < 0 ? -dd : dd;
            if (dd < best)
                best = dd, up = i;
        }
        k = (uint32_t)r * up + (uint32_t)c;
        return cmv_base() + cs.tonic + deg[k % n] + 12 * (int)(k / n);
    }
    default:
        return -1;
    }
}

/* the chord keys: the type pads as the FM-1 layout's type keys (F#3 G#3 A#3; SUS: G#3 + A#3, so MIN + MAJ is SUS,
 * the SUS window and the secret chords as on the keybed), the extension pads as chord keys (cu_mod_press's LOCK rules) */
static const uint8_t CMV_TK[3] = {CU_TKF, CU_TKG, CU_TKA};
static void cmv_tk(int i, int down)
{
    if (down) {
        if (!cmv.tk_ref[i]++)
            cu_tk_press(CMV_TK[i]);
    } else if (cmv.tk_ref[i] && !--cmv.tk_ref[i])
        cu_tk_release(CMV_TK[i]);
}
static uint32_t cmv_mods_phys(void)                /* cr_ui.c cu_mods_phys: what the Move's pads hold */
{
    return cmv.ext_held | (cu_fm1() ? 0u : cu.tposted);   /* (the FM-1 layout counts tposted itself) */
}
static void cmv_ext_press(uint32_t m)              /* an extension pad down: cr_ui.c cu_mod_press, by its cr_mod_t */
{
    uint32_t bit = 1u << m, x, others;
    cmv.ext_held |= (uint8_t)bit;
    others = cu_mods_phys() & ~bit;
    if (cu.mpend != bit)
        cu.mpend = 0;
    if (cu.tpend != bit)
        cu.tpend = 0;
    if (cu.clock && !others && cu.mlatch) {
        uint32_t keep = 0xFFu;
        if (cs.lock_push) {
            if ((cu.mlatch & ~CU_MOD_TOP) == bit)
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
                cr_post(CRE_MOD, x, 1, 0);
        cu.mlatch &= (uint8_t)keep;
    }
    cr_post(CRE_MOD, m, 0, 1);
    cu_mod_touch();
}
static void cmv_ext_release(uint32_t m)            /* .. up: cu_mod_release */
{
    uint32_t bit = 1u << m;
    cmv.ext_held &= (uint8_t)~bit;
    cu_mod_touch();
    if (cu.mkill & bit) {
        cu.mkill &= (uint8_t)~bit;
        return;
    }
    if ((cu.tpend | cu.mpend) & bit) {
        cu.mpend &= (uint8_t)~bit;
        cu.tpend &= (uint8_t)~bit;
        if (cu.clock) {
            cu.mlatch &= (uint8_t)~bit;
            cr_post(CRE_MOD, m, 0, 0);
            cu_mods_trace();
            return;
        }
    }
    if (cu.clock) {
        cu.mlatch |= (uint8_t)bit;
        cu_mods_trace();
        return;
    }
    cr_post(CRE_MOD, m, 0, 0);
}
static void cmv_mod(uint32_t j, int down)          /* chord pad j: DIM MIN MAJ SUS 6 m7 M7 9 */
{
    if (j < 4u) {
        uint32_t bit = 1u << j;
        if (down == !!(cmv.type_pads & bit))
            return;
        cmv.type_pads = (uint8_t)(down ? cmv.type_pads | bit : cmv.type_pads & ~bit);
        if (j == 3u) {                             /* SUS: the pair */
            cmv_tk(1, down);
            cmv_tk(2, down);
        } else
            cmv_tk((int)j, down);
    } else if (down)
        cmv_ext_press(j);
    else
        cmv_ext_release(j);
}

/* ------------------------------------------------------------ memories --- */
/* the bank's eight in the engine's chord pads (cr_pad_set; the FM-1 never uses them): a pad plays its chord as a key
 * does (performance, bass, the voicing now), a step held gets it through the gesture hook */
static void cmv_pads_sync(void)
{
    uint32_t j;
    fm1_irq_off();
    for (j = 0; j < CR_MAX_PADS; j++) {
        const cmv_mem_t *m = &cmv.mem[cmv.bank * 8u + j];
        int16_t r;
        uint8_t q, x, v;
        int have = cr_pad_get(&cr, (int)j, &r, &q, &x, &v);
        if (have != m->used || (m->used && (r != m->root || q != m->q || x != m->x || v != m->vel)))
            cr_pad_set(&cr, (int)j, m->used, m->root, m->q, m->x, m->vel);
    }
    fm1_irq_on();
    cmv.pads_dirty = 0;
}
static void cmv_mem_play(uint32_t j, int down)
{
    fm1_irq_off();
    if (down) {
        cr_pad_down(&cr, (int)j);
        cmv.lc_shadow = cr.last_chord;             /* (the last chord played is this memory's until a key plays) */
        cmv.lc_mem = (int8_t)(cmv.bank * 8u + j);
    } else
        cr_pad_up(&cr, (int)j);
    fm1_irq_on();
    cmv.mem_down = (uint8_t)(down ? cmv.mem_down | (1u << j) : cmv.mem_down & ~(1u << j));
}
/* the last chord played: root (the engine's: as played, without the KEY layer's Transpose; a progression's: its
 * reference), quality, extensions, velocity. 0: none yet */
static int cmv_last(cmv_mem_t *m)
{
    cr_chord_t lc;
    fm1_irq_off();
    lc = cmv.mine.used ? cmv.mine : cr.last_chord;
    fm1_irq_on();
    if (cmv.lc_mem >= 0 && lc.used == cmv.lc_shadow.used && lc.root == cmv.lc_shadow.root && lc.q == cmv.lc_shadow.q &&
        lc.x == cmv.lc_shadow.x && lc.vel == cmv.lc_shadow.vel) {
        *m = cmv.mem[cmv.lc_mem];
        return m->used;
    }
    cmv.lc_mem = -1;
    m->used = lc.used;
    m->root = lc.root;
    m->q = lc.q;
    m->x = lc.x;
    m->vel = lc.vel ? lc.vel : 100u;
    m->voic = 0;
    return lc.used;
}
static void cmv_msg(const char *t, const char *v);
static int cmv_clip_step(uint32_t s);               /* (proposal 5's clipboard, below) */
static void cmv_sc_tap(uint32_t i);
static void cmv_mem_save(uint32_t j, int pad)
{
    cmv_mem_t m;
    char nm[16], say[64], w[48];
    uint32_t k = cmv.bank * 8u + j;
    if (!cmv_last(&m)) {
        cmv_msg("Memory", "play a chord");
        return;
    }
    fm1_irq_off();
    m.voic = cr.voicing;
    fm1_irq_on();
    m.used = 1;
    cmv.mem[k] = m;
    cmv.pads_dirty = 1;
    cmv_mem_name(&m, nm, sizeof nm);
    cmv.saved_n = (uint8_t)(k + 1u);
    cmv_cpy(cmv.saved_name, nm, sizeof cmv.saved_name);
    cmv.saved_until = cmv_now() + CMV_MSG_MS;
    cmv.flash_pad = (int8_t)pad;
    cmv.flash_until = cmv_now() + CMV_FLASH_MS;
    cmv_cpy(say, "Memory ", sizeof say);
    cmv_int(w, (int32_t)k + 1, 0, sizeof w);
    cmv_cat(say, w, sizeof say);
    cmv_cat(say, ", ", sizeof say);
    cmv_words(nm, w, sizeof w);
    cmv_cat(say, w, sizeof say);
    cmv_say(say);
    cu_trace("move: memory %u saved: %s\n", (unsigned)k + 1u, nm);
}
static void cmv_mem_clear(uint32_t j)
{
    char t[24], v[8];
    uint32_t k = cmv.bank * 8u + j;
    cmv.mem[k].used = 0;
    cmv.pads_dirty = 1;
    cmv_cpy(t, "Memory ", sizeof t);
    cmv_int(v, (int32_t)k + 1, 0, sizeof v);
    cmv_cat(t, v, sizeof t);
    cmv_msg(t, "cleared");
    cu_trace("move: memory %u cleared\n", (unsigned)k + 1u);
}

/* the memories, the bank and the layouts in flash: a record of their own (cr_settings.c css_read / css_write call
 * cmv_store_load / cmv_store_save, FM1_HOST_TK): read with the session (power-on, a revert), written by the global
 * save when it changed. Where the FM-1's map has a free sector nothing uses (hal/fm1_flash_map.h: "0xF2000-0xF3FFF free
 * (unlisted), unused"); the host's flash file holds it, the FM-1's never sees it, and the session record stays the
 * FM-1's own (a build without the takeover reads it as ever). One copy: magic, length, version, FNV-1a of the
 * payload, then the payload */
#define CMV_STORE_OFF 0xF2000u
#define CMV_STORE_MAGIC 0x564D5243u                /* "CRMV" */
#define CMV_TAIL_LEN0 (8u + 7u * CMV_NMEM)            /* version 1 as first written */
#define CMV_TAIL_LEN (CMV_TAIL_LEN0 + 3u)               /* + the pads' velocity mode, Fixed's level (proposal 5), the
                                                         * loop's swing (proposal 6) */
#define CMV_STORE_HDR 16u
static uint32_t cmv_store_flash;                   /* the payload's hash in flash (0: none read or written) */
static void cmv_tail_put(uint8_t *d)
{
    uint32_t i;
    uint8_t *p = d + 8;
    d[0] = (uint8_t)CMV_STORE_MAGIC, d[1] = (uint8_t)(CMV_STORE_MAGIC >> 8);
    d[2] = (uint8_t)(CMV_STORE_MAGIC >> 16), d[3] = (uint8_t)(CMV_STORE_MAGIC >> 24);
    d[4] = 1;                                      /* version */
    d[5] = cmv.bank, d[6] = cmv.layout, d[7] = cmv.prev;
    for (i = 0; i < CMV_NMEM; i++, p += 7) {
        const cmv_mem_t *m = &cmv.mem[i];
        p[0] = m->used, p[1] = m->q, p[2] = m->x, p[3] = m->vel;
        p[4] = (uint8_t)m->root, p[5] = (uint8_t)((uint16_t)m->root >> 8), p[6] = (uint8_t)m->voic;
    }
    p[0] = cmv.vmode, p[1] = cmv.vfixed, p[2] = crl_swing;
}
static void cmv_tail_get(const uint8_t *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < CMV_NMEM; i++)
        cmv.mem[i].used = 0;
    cmv.bank = 0;
    cmv.layout = CMV_KEYS;
    cmv.prev = CMV_SCALE;
    cmv.vmode = 0;
    cmv.vfixed = 100;
    crl_swing = 50;
    if (s && n >= CMV_TAIL_LEN0 && s[0] == (uint8_t)CMV_STORE_MAGIC && s[1] == (uint8_t)(CMV_STORE_MAGIC >> 8) &&
        s[2] == (uint8_t)(CMV_STORE_MAGIC >> 16) && s[3] == (uint8_t)(CMV_STORE_MAGIC >> 24) && s[4] == 1u) {
        const uint8_t *p = s + 8;
        cmv.bank = (uint8_t)(s[5] & 7u);
        cmv.layout = (uint8_t)(s[6] < CMV_NLAYOUT ? s[6] : CMV_KEYS);
        cmv.prev = (uint8_t)(s[7] < CMV_NLAYOUT ? s[7] : CMV_SCALE);
        for (i = 0; i < CMV_NMEM; i++, p += 7) {
            cmv_mem_t *m = &cmv.mem[i];
            m->used = (uint8_t)(p[0] == 1u && (p[1] < CR_Q_COUNT || p[1] == CR_Q_PROG));
            m->q = p[1], m->x = (uint8_t)(p[2] & 15u), m->vel = (uint8_t)(p[3] ? p[3] & 127u : 100u);
            m->root = (int16_t)(p[4] | p[5] << 8), m->voic = (int8_t)p[6];
        }
        if (n >= CMV_TAIL_LEN0 + 2u) {
            cmv.vmode = (uint8_t)(p[0] < CMV_V_N ? p[0] : 0u);
            cmv.vfixed = (uint8_t)(p[1] >= 1u && p[1] <= 127u ? p[1] : 100u);
        }
        if (n >= CMV_TAIL_LEN0 + 3u)
            crl_swing = (uint8_t)(p[2] >= 50u && p[2] <= 75u ? p[2] : 50u);
    }
    if (cmv.layout == CMV_SCALE)                   /* (left in SCALE: Key Mode is the session's own) */
        cmv.layout = cmv.prev == CMV_SCALE ? CMV_KEYS : cmv.prev;
    cmv.pads_dirty = 1;
}
static uint32_t cmv_fnv(const uint8_t *b, uint32_t a, uint32_t e)
{
    uint32_t h = 2166136261u;
    for (; a < e; a++) {
        h ^= b[a];
        h *= 16777619u;
    }
    return h | 1u;                                 /* (never 0: "none") */
}
static uint32_t cmv_rd32(const uint8_t *b) { return (uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24; }
static void cmv_wr32(uint8_t *b, uint32_t v) { b[0] = (uint8_t)v, b[1] = (uint8_t)(v >> 8), b[2] = (uint8_t)(v >> 16), b[3] = (uint8_t)(v >> 24); }
static uint8_t cmv_sbuf[CMV_STORE_HDR + CMV_TAIL_LEN] __attribute__((aligned(4)));
static void cmv_store_load(void)
{
    uint32_t n;
    cmv_store_flash = 0;
    if (!flash_ok || ST_BLOCKED() || st_read(CMV_STORE_OFF, cmv_sbuf, sizeof cmv_sbuf)) {
        cmv_tail_get(0, 0);
        return;
    }
    n = (uint32_t)cmv_sbuf[4] | (uint32_t)cmv_sbuf[5] << 8;
    if (cmv_rd32(cmv_sbuf) != CMV_STORE_MAGIC || (n != CMV_TAIL_LEN && n != CMV_TAIL_LEN0 && n != CMV_TAIL_LEN0 + 2u) ||
        cmv_fnv(cmv_sbuf, CMV_STORE_HDR, CMV_STORE_HDR + n) != cmv_rd32(cmv_sbuf + 8)) {
        cmv_tail_get(0, 0);                        /* (none, or not whole: the defaults) */
        return;
    }
    cmv_tail_get(cmv_sbuf + CMV_STORE_HDR, n);
    cmv_store_flash = cmv_rd32(cmv_sbuf + 8);
}
static int cmv_store_save(void)                    /* 0: written, or unchanged */
{
    uint32_t h, at;
    cmv_tail_put(cmv_sbuf + CMV_STORE_HDR);
    h = cmv_fnv(cmv_sbuf, CMV_STORE_HDR, CMV_STORE_HDR + CMV_TAIL_LEN);
    if (h == cmv_store_flash)
        return 0;
    cmv_wr32(cmv_sbuf, CMV_STORE_MAGIC);
    cmv_sbuf[4] = (uint8_t)CMV_TAIL_LEN, cmv_sbuf[5] = (uint8_t)(CMV_TAIL_LEN >> 8);
    cmv_sbuf[6] = 1, cmv_sbuf[7] = 0;
    cmv_wr32(cmv_sbuf + 8, h);
    cmv_wr32(cmv_sbuf + 12, 0);
    if (st_erase(CMV_STORE_OFF))
        return -1;
    for (at = 0; at < sizeof cmv_sbuf; at += 256u)  /* (a page at a time: a program never wraps a page) */
        if (st_prog(CMV_STORE_OFF + at, cmv_sbuf + at, sizeof cmv_sbuf - at < 256u ? sizeof cmv_sbuf - at : 256u))
            return -1;
    cmv_store_flash = h;
    cu_trace("move: memories saved (0x%x)\n", (unsigned)CMV_STORE_OFF);
    return 0;
}

/* ------------------------------------------------------------ messages --- */
static void cmv_msg(const char *t, const char *v)
{
    char s[64], w[40];
    cmv_cpy(cmv.msg_t, t, sizeof cmv.msg_t);
    cmv_cpy(cmv.msg_v, v, sizeof cmv.msg_v);
    cmv.msg_until = cmv_now() + CMV_MSG_MS;
    cmv.pop_kind = -1;
    cmv_cpy(s, t, sizeof s);
    if (*t && *v)
        cmv_cat(s, ", ", sizeof s);
    cmv_vwords(v, w, sizeof w);
    cmv_cat(s, w, sizeof s);
    cmv_say(s);
}
static void cmv_mirror_msg(void)                   /* the FM-1 UI's message an action of ours caused ("saved", "end the
                                                    * take first", "loop 2: empty"): the Move shows it too */
{
    if (cu.msg.until != cmv.msg_seen && (int32_t)(cu.msg.until - cmv_now()) > 0 && cu.msg.text[0]) {
        char t[24], *dot;
        cmv.msg_seen = cu.msg.until;
        cmv_cpy(t, cu.msg.text, sizeof t);
        dot = t;
        while (*dot && (unsigned char)*dot != 0xB7u && *dot != ':')
            dot++;
        if (cu.msg.big)                            /* PANIC: its label under it */
            cmv_msg(t, cu.msg.label);
        else if (*dot) {                           /* "loop 2 \267 saved 1", "loop 2: empty": two lines */
            char *v = dot + 1;
            *dot = 0;
            while (dot > t && dot[-1] == ' ')
                *--dot = 0;
            while (*v == ' ')
                v++;
            cmv_msg(t, v);
        } else
            cmv_msg("", t);
    }
}

/* ---------------------------------------------------------- the layouts --- */
static void cmv_set_layout(uint32_t l)
{
    char s[40];
    if (l >= CMV_NLAYOUT)
        return;
    if (l != cmv.layout) {
        cmv.prev = cmv.layout;
        cmv.layout = (uint8_t)l;
    }
    if (cmv.layout == CMV_SCALE && !cmv.scale_keyed) {   /* SCALE plays the key's chords: Key Mode on while it shows */
        cmv.key_was = cs.key_on;
        cmv.scale_keyed = 1;
        if (!cs.key_on) {
            cs.key_on = 1;
            cu_post_key();
        }
    } else if (cmv.layout != CMV_SCALE && cmv.scale_keyed) {
        cmv.scale_keyed = 0;
        if (!cmv.key_was && cs.key_on) {
            cs.key_on = 0;
            cu_post_key();
        }
    }
    cmv_cpy(s, "Layout ", sizeof s);
    cmv_cat(s, CMV_LAYOUT_SAY[cmv.layout], sizeof s);
    cmv_say(s);
    cu_trace("move: layout %s (prev %s)\n", CMV_LAYOUT[cmv.layout], CMV_LAYOUT[cmv.prev]);
}

/* ------------------------------------------------------------- the steps --- */
static void cmv_map_sync(void)
{
    if (cmv.map_ok && cmv.map_gen == crl.gen && cmv.map_page == cmv.page)
        return;
    fm1_irq_off();
    cr_loop_ed_map(&crl, cmv.page * 16u, &cmv.map);
    cmv.map_gen = crl.gen;
    fm1_irq_on();
    cmv.map_page = cmv.page;
    cmv.map_ok = 1;
}
static uint32_t cmv_nsteps(void)                   /* the loop's steps (an empty slot: a bar of 16, as the editor opens it) */
{
    cmv_map_sync();
    return cmv.map.nstep ? cmv.map.nstep : 16u;
}
static int cmv_step_has(uint32_t i)                /* step i of the page holds a chord */
{
    cmv_map_sync();
    return (int)((cmv.map.start >> i) & 1u);
}
#define CMV_BAR (16u * CRL_STEP)
static int cmv_playing(void) { return cr_snap.lstate == CRL_PLAYING || cmv.ghost; }
static int cmv_playhead(void)                      /* the step playing (of the loop), -1 none */
{
    if (cmv.ghost)
        return (int)(((crl.rt - cmv.ghost_rt0) % CMV_BAR) / CRL_STEP);
    if (cr_snap.lstate != CRL_PLAYING || !cr_snap.llen)
        return -1;
    return (int)(cr_snap.lpos / CRL_STEP);
}
/* the step's name and properties for the step view (cu_le, the editor's own) */
static void cmv_step_say(void)
{
    char s[64], w[40], nm[16], r[4], q[5], sup[CR_SUP_MAX], n[8];
    cmv_cpy(s, "Step ", sizeof s);
    cmv_int(n, cmv.step + 1, 0, sizeof n);
    cmv_cat(s, n, sizeof s);
    if (cu_le.cnt) {
        cu_ev_name(cu_le.root, cu_le.qx, r, q, sup);
        cmv_chord_str(nm, sizeof nm, r, q, sup);
        cmv_words(nm, w, sizeof w);
        cmv_cat(s, ", ", sizeof s);
        cmv_cat(s, w, sizeof s);
    } else
        cmv_cat(s, ", empty", sizeof s);
    cmv_say(s);
}
/* a step written from the last chord (a tap on an empty step): the editor's write, as a gesture's end at its cursor */
static void cmv_step_write_last(void)
{
    cmv_mem_t m;
    int16_t root;
    if (!cmv_last(&m)) {
        cmv_msg("Step", "play a chord");
        return;
    }
    root = m.q == CR_Q_PROG ? m.root : (int16_t)(m.root + cs.transpose);   /* (the gesture's root: as played + the
                                                    * Transpose, as crx_gesture gives the looper) */
    fm1_irq_off();
    cr_loop_gesture(&crl, CMV_GID, root, m.q, m.x, m.vel, 1);
    cr_loop_gesture(&crl, CMV_GID, root, m.q, m.x, m.vel, 0);
    fm1_irq_on();
    cu_trace("move: step %d gets the last chord (root %d q %u x %u)\n", cmv.step + 1, (int)root, m.q, m.x);
}
static void cmv_step_press(uint32_t i)
{
    uint32_t s = cmv.page * 16u + i, n;
    if (cmv.step >= 0)
        return;
    if (!cu.ledit) {
        cu_ledit_open();                           /* (refused while a take or step entry is open: its message) */
        if (!cu.ledit) {
            cmv_mirror_msg();
            return;
        }
        cmv.step_ed = 1;
        cmv.step_lock = cu.lock;                   /* the PLAY menu under it, as SEQ's tap opens it (cu_seq_tap) */
        cu.lock = L_LOOP;
    }
    cmv.step = (int8_t)s;
    cmv.step_btn = (uint8_t)i;
    cmv.step_t0 = cmv_now();
    cmv.step_used = 0;
    cmv.step_ext = 0;
    cu_le.ok = 0;
    cu.le_cell = (uint16_t)s;
    cu_le_sync();
    n = cu_le.nstep;
    if (s >= n) {                                  /* past the loop's end: nothing (the owner, 2026-11-20: the Steps
                                                    * knob makes the loop longer), the editor closed as a release does */
        cmv.step = -1;
        if (cmv.step_ed) {
            cu_ledit_close();
            if (cu.lock == L_LOOP)
                cu.lock = cmv.step_lock;
        }
        cmv.step_ed = 0;
        cmv.map_ok = 0;
        return;
    }
    cmv.step_prev = 0;
    if (cmv_clip_step(s)) {                        /* Copy / Delete held, a paste armed: the clipboard (proposal 5) */
        cmv.step_used = 1;
        cmv.step_prev = 1;
        cu.le_cell = (uint16_t)s;
        cu_le.ok = 0;
        cu_le_sync();
        cmv.step_gen = crl.gen;
        return;
    }
    cu_le.ok = 0;
    cu_le_sync();
    cmv_step_say();
    cmv.step_gen = crl.gen;
    cu_trace("move: step %u held (%u events)\n", (unsigned)s + 1u, (unsigned)cu_le.cnt);
}
static void cmv_step_release(uint32_t s)          /* the step held (of the loop) let go */
{
    if (cmv.step < 0 || (uint32_t)cmv.step != s)
        return;
    cu_le.ok = 0;
    cu_le_sync();
    if (!cmv.step_used && cmv_now() - cmv.step_t0 < CMV_HOLD_MS) {   /* a tap: empty gets the last chord, else cleared */
        if (cu_le.cnt)
            cu_ledit_clear();
        else
            cmv_step_write_last();
        cu_le.ok = 0;
        cu_le_sync();
    }
    if (cmv.step_ext && !cu_le.cnt) {              /* made longer for it and it stayed empty: the length as it was */
        fm1_irq_off();
        cr_loop_ed_len(&crl, (uint32_t)(cmv.step_ext - 1u) * CRL_STEP);
        fm1_irq_on();
    }
    cmv.step = -1;
    if (cmv.step_ed) {
        cu_ledit_close();
        if (cu.lock == L_LOOP)
            cu.lock = cmv.step_lock;
    }
    cmv.step_ed = 0;
    cmv.map_ok = 0;
    if (cmv.pop_kind >= K_S_LEN && cmv.pop_kind <= K_S_STEPS)
        cmv.pop_kind = -1;
}

/* ------------------------------------------- the FM-1's layers, its editor --- */
/* The settings are the FM-1's own (proposal 4: its menu language): a layer open is cu.lock (the FM-1 shows it too),
 * its knobs and pages are cu_knob's (KNOB 1..4, SELECT), its names and values its screen builder's (cu_layer_screen,
 * ce_screen, cu_options_screen) into a scratch screen. The FM-1 opening or closing one shows here the same way */
static cr_screen_t cmv_ls;
static uint32_t cmv_layer(void)                    /* the settings layer open, 0 none (the step editor's L_LOOP: none) */
{
    const uint32_t l = cu.lock;
    return l == L_KEY || l == L_LOCK || l == L_PERF || l == L_FX || l == L_BASS || l == L_METRO ? l : 0u;
}
static int cmv_opts(void) { return cu.opt_open && !cu.lock && !cu.page; }
static int cmv_editing(void) { return cu.page == PG_EDIT && !cu.lock; }
static const cr_screen_t *cmv_lscreen(void)
{
    memset(&cmv_ls, 0, sizeof cmv_ls);
    if (cmv_opts())
        cu_options_screen(&cmv_ls);
    else if (cmv_layer())
        cu_layer_screen(&cmv_ls, cmv_layer());
    else if (cmv_editing())
        ce_screen(&cmv_ls, cmv_now());
    return &cmv_ls;
}
/* the editor's knob k: 1-4 the lane on the knobs, 5-8 the next lane of the screen (none past its last) */
static int cmv_ed_lane(uint32_t k, uint8_t **lane, uint8_t *was)
{
    track_t *t = ce_trk();
    uint32_t p = ce.part & 1u, g;
    ce_scr_t sc;
    g = ce_fix(t, p, &sc);
    *lane = &cx.lane[p][g];
    *was = **lane;
    if (k < 4u)
        return 1;
    if ((uint32_t)*was + 1u >= ce_lanes(t, g, &sc))
        return 0;
    **lane = (uint8_t)(*was + 1u);
    return 1;
}
static int cmv_ed_cell(uint32_t k, cr_cell_t *cl)
{
    track_t *t = ce_trk();
    ce_view_t vw;
    uint8_t *lane, was;
    memset(cl, 0, sizeof *cl);
    if (!cmv_ed_lane(k, &lane, &was))
        return 0;
    ce_view(t, ce.part & 1u, &vw);
    ce_cell(t, &vw, vw.ref[vw.active][k & 3u], cl);
    if (!cl->label[0] && vw.kind == CR_K_STACK)    /* (a stack's cells: its column headings) */
        cmv_cpy(cl->label, vw.head[k & 3u], sizeof cl->label);
    *lane = was;
    return (cl->flags & CR_CF_ON) && cl->label[0];
}
/* a settings / editor / Options knob: its name and value (0: no knob there) */
static int cmv_lay_cell(uint32_t k, char *t, uint32_t tn, char *v, uint32_t vn, int32_t *pct)
{
    cr_cell_t cl;
    *pct = -1;
    if (cmv_opts()) {                              /* KNOB 1 alone: the entry's value (the group list: the group) */
        if (k)
            return 0;
        if (!cu.opt_in) {
            cmv_cpy(t, "Group", tn);
            cmv_cpy(v, CU_G_NAME[cu.opt_g], vn);
        } else {
            cmv_cpy(t, O_NAME[cu.opt_sel], tn);
            opt_text(cu.opt_sel, v, vn);
        }
        return 1;
    }
    if (cmv_layer()) {
        if (k >= 4u)
            return 0;
        cl = cmv_lscreen()->cell[0][k];
        if (!(cl.flags & CR_CF_ON) || !cl.label[0])
            return 0;
        if (cl.glyph == CR_G_BAR || cl.glyph == CR_G_SHIFT)
            *pct = (int32_t)cl.pct * 1000 / 255;
    } else if (cmv_editing()) {
        if (!cmv_ed_cell(k, &cl))
            return 0;
        *pct = (int32_t)cl.pct * 1000 / 255;
    } else
        return 0;
    cmv_cpy(t, cl.label, tn);
    cmv_cpy(v, cl.value, vn);
    return 1;
}
static void cmv_lay_turn(uint32_t k, int32_t s)
{
    if (cmv_opts()) {
        if (!k)
            cu_knob(EN_K1, s);
    } else if (cmv_layer()) {
        if (k < 4u)
            cu_knob(EN_K1 + k, s);                 /* (the FM-1's own: cu_layer_knob of the layer open) */
    } else if (cmv_editing()) {
        uint8_t *lane, was;
        if (cmv_ed_lane(k, &lane, &was)) {
            ce_knob(k & 3u, s, 0);
            *lane = was;
        }
    }
}
static const char *cmv_layer_name(uint32_t l)
{
    switch (l) {
    case L_KEY: return "KEY";
    case L_LOCK: return "LOCK";
    case L_PERF: return cu.perf_part ? "B.PERF" : "PERF";
    case L_FX: return "FX";
    case L_BASS: return "BASS";
    case L_METRO: return "BEAT";
    default: return "";
    }
}
static int cmv_layer_on(uint32_t l)
{
    switch (l) {
    case L_KEY: return cs.key_on;
    case L_LOCK: return cu.clock;
    case L_PERF: return cu.perf_part ? !cu_pf_off(CU_PF_BASS) : cs.perform_on;
    case L_FX: return cs.fx_on;
    case L_BASS: return cs.bass_on;
    case L_METRO: return cs.metro;
    default: return 0;
    }
}
/* the layer's page: its name, which of how many */
static void cmv_layer_page(char *d, uint32_t n, uint32_t *at, uint32_t *of)
{
    static const char *const KEYPG[3] = {"Key", "Keyboard", "Progs"};
    const cr_screen_t *s = cmv_lscreen();
    *at = 0, *of = 1;
    if (cmv_layer() == L_KEY) {
        cmv_cpy(d, KEYPG[cu.kpage % 3u], n);
        *at = cu.kpage % 3u, *of = 3;
    } else if (s->n_items) {
        cmv_cpy(d, s->item[(uint32_t)(s->sel - s->item0) % CR_PICK_MAX], n);
        *at = s->sel, *of = s->n_items;
    } else
        cmv_cpy(d, cmv_layer_name(cmv_layer()), n);
}
static void cmv_layer_say(void)
{
    char s[48], pg[24];
    uint32_t at, of;
    cmv_layer_page(pg, sizeof pg, &at, &of);
    cmv_cpy(s, cmv_layer_name(cmv_layer()), sizeof s);
    cmv_cat(s, ", ", sizeof s);
    cmv_cat(s, pg, sizeof s);
    cmv_say(s);
}
static void cmv_layer_open(uint32_t l, uint32_t part)   /* cr_ui.c cu_opened, for a layer by its number */
{
    const uint32_t was = cu.lock;
    cu.opt_open = 0;
    if (cu.page != PG_EDIT)                        /* (over the editor: Back returns to it, as on the FM-1) */
        cu.page = PG_NONE;
    if (was == L_EDIT)
        cu_pick_end(1);
    cu.lock = (uint8_t)l;
    cu_hot.knob = 0;
    if (l == L_METRO && was != L_METRO)
        cu.mpage = 0;
    if (l == L_PERF) {
        if (was != L_PERF)
            cu.perf_t0 = cu_now();
        cu_perf_layer_part(part);
    }
    cmv.pop_kind = -1;
    cmv.msg_until = cmv.wheel_until = 0;
    cmv_layer_say();
    cu_trace("move: settings %s\n", cmv_layer_name(l));
}
static void cmv_card(const char *t, const char *v)   /* a card for a moment, not said (the caller says it) */
{
    cmv_cpy(cmv.msg_t, t, sizeof cmv.msg_t);
    cmv_cpy(cmv.msg_v, v, sizeof cmv.msg_v);
    cmv.msg_until = cmv_now() + CMV_MSG_MS;
    cmv.pop_kind = -1;
}
static void cmv_home(void)                         /* Back (proposal 5): home from anywhere, the knobs the home's again */
{
    int was = cmv_opts() || cmv_layer() || cmv_editing() || cmv.set_open;
    if (cmv.set_edit) {                            /* (the Settings list editing a value: the value kept, its edit ends) */
        cmv.set_edit = 0;
        cmv_say("Done");
        return;
    }
    cmv.set_open = 0;
    if (cmv_opts())
        cu_close_all();
    if (cmv_layer())
        cu_layer_close();
    if (cmv_editing()) {
        ce_close();
        cmv_card("", "MEMORY");
    }
    cu.opt_open = 0;
    cmv.wheel_eng = cmv.wheel_beng = 0;
    if (was)
        cmv_say("Home");
}
static const char *const CMV_GRP_SAY[CE_NSEC] = {"Oscillators", "Filter", "Envelopes", "LFO", "Modulation", "Effects",
                                                  "Mix"};
static void cmv_edit_toggle(uint32_t part)         /* Shift + S8: the sound editor (the top right's pads EDIT); again: the
                                                    * memories (MEMORY, proposal 6) */
{
    char s[64];
    if (cmv_editing()) {
        ce_close();
        cmv_card("", "MEMORY");
        cmv_say("Memories");
        return;
    }
    cmv.set_open = cmv.set_edit = 0;
    if (cmv_opts())
        cu_close_all();
    if (cmv_layer())
        cu_layer_close();
    cu_edit_open(part);
    cmv.pop_kind = -1;
    cmv_card(ce.part ? "Bass" : "Chord", "EDIT");   /* a card for a moment: what the pads are now */
    cmv_cpy(s, ce.part ? "Sound editor, bass, " : "Sound editor, chord, ", sizeof s);
    cmv_cat(s, CMV_GRP_SAY[cx.grp[ce.part & 1u] % CE_NSEC], sizeof s);
    cmv_say(s);
}
static void cmv_ed_group(uint32_t j)               /* a group's pad: as its FM-1 button (tapped again: its next screen) */
{
    const char *const *SAY = CMV_GRP_SAY;
    char s[40], v[8];
    if (j >= CE_NSEC) {                            /* the eighth: chord <-> bass */
        ce.part ^= 1u;
        cx.hot_r = 0;
        cmv_say(ce.part ? "Editor, bass" : "Editor, chord");
        return;
    }
    ce_group(j);
    cmv_mirror_msg();
    cmv_cpy(s, SAY[j], sizeof s);
    cmv_cat(s, " ", sizeof s);
    cmv_int(v, cx.scr[ce.part & 1u][j] + 1, 0, sizeof v);
    cmv_cat(s, v, sizeof s);
    if (cx.grp[ce.part & 1u] == j && (int32_t)(cmv.msg_until - cmv_now()) <= 0)
        cmv_say(s);
}

/* ------------------------------------------------------------- the knobs --- */
static uint32_t cmv_perf_idx(void) { return cs.perform_on ? cs.perf_sel + 1u : 0u; }
static int cmv_knob_kind(uint32_t k)
{
    static const uint8_t HOME[8] = {K_VOIC, K_BREG, K_RATE, K_FXSEND, K_KEY, K_SCALE, K_TRANS, K_TEMPO};
    static const uint8_t LOOP[8] = {K_L_LEN, K_L_RATE, K_L_QUANT, K_L_CIN, K_L_LEVEL, K_L_BEAT, K_L_MODE, K_TEMPO};
    if (cmv.step >= 0) {
        static const uint8_t ST[8] = {K_S_LEN, K_S_CHANCE, K_S_VEL, K_S_RAT, K_S_COND, K_S_VOIC, K_S_TR, K_S_STEPS};
        return ST[k];
    }
    if (cmv.loop)
        return LOOP[k];
    if (cmv.set_open || cmv_opts() || cmv_layer() || cmv_editing())
        return (int)(K_LAY0 + k);
    return HOME[k];                                /* (one layer: Shift does not change them, proposal 4 A) */
}
static void cmv_sound_turn(int32_t s)              /* the chord part's sound, as PRESETS (cu_knob EN_PRESET) */
{
    uint32_t n = pool_count(trk[CR_PART_CHORD].eng_req % NENGINES);
    if (cuf_n(0, trk[CR_PART_CHORD].eng_req))
        cu_sound_go((uint32_t)cuf_move(0, (int32_t)cs.sound, s, 1, 0));
    else
        cu_sound_go((cs.sound + (uint32_t)(s % (int32_t)n + (int32_t)n)) % n);
}
/* a step's knob: cr_ui.c cu_ledit_knob on its page (Step: Note len, Steps, Chance, Velocity; Trig: Ratchet, Condition,
 * Voicing, Transpose) */
static void cmv_step_knob(int kind, int32_t s)
{
    static const uint8_t PG[8] = {0, 0, 0, 1, 1, 1, 1, 0}, KN[8] = {0, 2, 3, 0, 1, 2, 3, 1};
    uint32_t i = (uint32_t)(kind - K_S_LEN);
    cu.le_page = PG[i];
    cu_le.ok = 0;
    cu.le_cell = (uint16_t)cmv.step;
    cu_le_sync();
    cu_ledit_knob(KN[i], s);
    cu_le.ok = 0;
    cu_le_sync();
    cmv.step_used = 1;
}
static void cmv_turn(int kind, int32_t s)
{
    int32_t v;
    uint32_t mode = cu_perf_mode(CU_PF_CHORD);
    switch (kind) {
    case K_VOIC:                                   /* (the engine's own step, now: the popup reads it back; KNOB 1
                                                    * posts CRE_VOICING, the same call in the audio's turn) */
        cr_voicing_step(&cr, s);
        break;
    case K_PERF:
        v = (int32_t)cmv_perf_idx() + (s > 0 ? 1 : -1);
        v = v < 0 ? 0 : v > 7 ? 7 : v;
        if (!v) {
            if (cs.perform_on)
                cu_perf_toggle();
        } else {
            cu_perf_pick((uint32_t)v - 1u);
            if (!cs.perform_on)
                cu_perf_toggle();
        }
        break;
    case K_RATE: {                                 /* the FM-1's KNOB 3: the shown part's mode's main value */
        uint32_t pp = cu_perf_view_part(), m = cu_perf_mode(pp);
        cu_param_turn(pp, m, CU_PERF_MAIN[m], s, 0);
        break;
    }
    case K_FXSEND: {                               /* the FM-1's KNOB 4: the chord part's send to the effect selected */
        uint32_t sid = CU_FX[cs.fx_sel].send;
        v = cu_send(0, sid);
        cu_send_set(0, sid, cu_send_step(v, s, 0));
        if (cu_send(0, sid) != v)
            cu_cc_send_trk(0, sid);
        break;
    }
    case K_L_LEN: case K_L_RATE: case K_L_QUANT: case K_L_CIN: case K_L_LEVEL: case K_L_BEAT: {
        uint8_t was = cu.lpage;                    /* the PLAY menu's knobs, by its page (cu_layer_knob L_LOOP) */
        if (cu.ledit)
            break;
        cu.lpage = (uint8_t)(kind >= K_L_LEVEL);
        cu_layer_knob(L_LOOP, (uint32_t)(kind >= K_L_LEVEL ? kind - K_L_LEVEL : kind - K_L_LEN), s);
        cu.lpage = was;
        break;
    }
    case K_L_MODE: cu_loop_mode_turn(s); break;
    case K_DIR: cu_param_turn(CU_PF_CHORD, mode, mode == CR_PM_PATTERN ? CR_P_DIV : CR_P_DIR, s, 0); break;
    case K_BREG: cr_bass_voicing_step(&cr, s); break;   /* (as the voicing) */
    case K_KEY:                                    /* the tonic (the KEY layer's KNOB 1: Key Mode on) */
        cs.tonic = (uint8_t)((cs.tonic + 12 + s % 12) % 12);
        cs.key_on = 1;
        cu_post_key();
        break;
    case K_SCALE:
        v = (int32_t)cs.scale + s;
        cs.scale = (uint8_t)(v < 0 ? 0 : v >= (int32_t)CR_SCALE_COUNT ? CR_SCALE_COUNT - 1 : v);
        cu_post_key();
        break;
    case K_TRANS:
        v = cs.transpose + s;
        cs.transpose = (int8_t)(v < -24 ? -24 : v > 24 ? 24 : v);
        cr_post(CRE_TRANSPOSE, 0, 0, cs.transpose);
        cmv.pads_dirty = 1;
        break;
    case K_SOUND: cmv_sound_turn(s); break;
    case K_BSOUND: {                               /* OFF, then the bass part's pool (cu_knob EN_ALGO) */
        int32_t n = (int32_t)pool_count(trk[CR_PART_BASS].eng_req % NENGINES) + 1;
        v = ((int32_t)cs.bass_sound + s % n + n) % n;
        if (cuf_n(1, trk[CR_PART_BASS].eng_req))
            v = cuf_move(1, (int32_t)cs.bass_sound - 1, s, 1, 1) + 1;
        cu_bass_go((uint32_t)v);
        break;
    }
    case K_GATE: cu_param_turn(CU_PF_CHORD, mode, CR_P_GATE, s, 0); break;
    case K_SWING: cu_param_turn(CU_PF_CHORD, mode, CR_P_SWING, s, 0); break;
    case K_BLEVEL:
        v = trk[CR_PART_BASS].p[P_LEVEL];
        trk[CR_PART_BASS].p[P_LEVEL] = (int16_t)cu_kval(trk[CR_PART_BASS].p[P_LEVEL], s, 0, 127, 0);
        if (trk[CR_PART_BASS].p[P_LEVEL] != v)
            cu_cc_send_trk(1, P_LEVEL);
        break;
    case K_VEL:                                    /* the progressions' Velocity 50..150 % (KEY page 3) */
        v = cu_kval((int32_t)cu_pvel(), s, 50, 150, 0);
        cs.prog_vel = (uint8_t)(v == 100 ? 0 : v);
        break;
    case K_HUM:
        v = cu_kval((int32_t)cs.prog_hum, s, 0, 100, 0);
        cs.prog_hum = (uint8_t)v;
        cr_post(CRE_PROGHUM, 0, 0, cs.prog_hum);
        break;
    case K_TEMPO:
        if (!(cr_route.clock_in && crl.ext_on))    /* (the clock's tempo is shown, not changed) */
            cu_set_tempo((int32_t)cs.bpm + s);
        break;
    default:
        if (kind >= K_LAY0)
            cmv_lay_turn((uint32_t)(kind - K_LAY0), s);
        else if (kind >= K_S_LEN && kind <= K_S_STEPS && cmv.step >= 0)
            cmv_step_knob(kind, s);
        break;
    }
    if (kind < K_LAY0)
        cu_trace("move: knob %s %+d\n", CMV_KNAME[kind], (int)s);
}
/* a knob's value as the popup shows it; *pct: its place 0..1000 for the bar, -1 none */
static void cmv_value(int kind, char *d, uint32_t n, int32_t *pct)
{
    char t[24], sub[12];
    int32_t v = 0, lo = 0, hi = 0, p;
    uint32_t mode = cu_perf_mode(CU_PF_CHORD);
    *pct = -1;
    switch (kind) {
    case K_VOIC:
        fm1_irq_off();
        v = cr.voicing;
        fm1_irq_on();
        cmv_int(d, v, v != 0, n);
        lo = -12, hi = 12;
        break;
    case K_PERF: cmv_cpy(d, CMV_PERF[cmv_perf_idx()], n); return;
    case K_RATE:
    case K_DIR:
    case K_GATE:
    case K_SWING: {
        uint32_t pp = kind == K_RATE ? cu_perf_view_part() : CU_PF_CHORD;
        mode = cu_perf_mode(pp);
        p = kind == K_RATE ? CU_PERF_MAIN[mode] : kind == K_DIR ? (mode == CR_PM_PATTERN ? CR_P_DIV : CR_P_DIR) :
            kind == K_GATE ? CR_P_GATE : CR_P_SWING;
        v = cu_pf_par(pp)[mode][p];
    }
        if (p == CR_P_DIV)
            cmv_cpy(d, CU_DIV[v % CR_DIV_COUNT], n);
        else if (p == CR_P_DIR)
            cmv_cpy(d, CU_DIR[v % 6], n);
        else if (p == CR_P_PATTERN)
            cmv_cpy(d, cr_pattern_name(v), n);
        else {
            cu_param_text((uint32_t)p, v, t, sub);
            cmv_cpy(d, t, n);
            cmv_cat(d, sub[0] == '%' ? "%" : sub[0] ? " " : "", n);
            if (sub[0] && sub[0] != '%')
                cmv_cat(d, sub, n);
            lo = CU_PAR[p].min, hi = CU_PAR[p].max;
        }
        break;
    case K_BREG:
        fm1_irq_off();
        v = cr.bass_voicing;
        fm1_irq_on();
        cmv_int(d, v, v != 0, n);
        cmv_cat(d, " oct", n);
        lo = -2, hi = 4;
        break;
    case K_KEY: cmv_cpy(d, CU_NOTE[cs.tonic % 12u], n); return;
    case K_SCALE: cmv_cpy(d, cr_scale_name(cs.scale), n); return;
    case K_TRANS:
        v = cs.transpose;
        cmv_int(d, v, v != 0, n);
        lo = -24, hi = 24;
        break;
    case K_SOUND: {
        uint32_t e = trk[CR_PART_CHORD].eng_req % NENGINES;
        char nm[13];
        pool_name(e, cs.sound, nm);
        cmv_cpy(d, nm, n);
        return;
    }
    case K_BSOUND:
        if (!cs.bass_sound || !cs.bass_on)
            cmv_cpy(d, "Off", n);
        else {
            char nm[13];
            pool_name(trk[CR_PART_BASS].eng_req % NENGINES, cs.bass_sound - 1u, nm);
            cmv_cpy(d, nm, n);
        }
        return;
    case K_BLEVEL: v = trk[CR_PART_BASS].p[P_LEVEL], hi = 127; cmv_int(d, v, 0, n); break;
    case K_VEL: v = (int32_t)cu_pvel(), lo = 50, hi = 150; cmv_int(d, v, 0, n); cmv_cat(d, "%", n); break;
    case K_HUM: v = cs.prog_hum, hi = 100; cmv_int(d, v, 0, n); cmv_cat(d, "%", n); break;
    case K_TEMPO: v = cs.bpm, lo = 20, hi = 300; cmv_int(d, v, 0, n); cmv_cat(d, " bpm", n); break;
    case K_FXSEND:                                 /* "Reverb 40%" */
        v = cu_send(0, CU_FX[cs.fx_sel].send);
        cmv_cpy(d, CU_FX[cs.fx_sel].name, n);
        cmv_cat(d, " ", n);
        cu_send_text(v, t, sizeof t);
        cmv_cat(d, t, n);
        *pct = (int32_t)cu_send_pct(v) * 10;
        return;
    case K_L_MODE: cmv_cpy(d, CU_LOOP_MODE[cs.loop_mode % CRL_NMODE], n); return;
    case K_L_LEN: case K_L_RATE: case K_L_QUANT: case K_L_CIN: case K_L_LEVEL: case K_L_BEAT: {
        uint8_t was = cu.lpage;                    /* (the PLAY menu's own cells: cu_loop_cells) */
        const cr_cell_t *c = &cmv_ls.cell[0][kind >= K_L_LEVEL ? kind - K_L_LEVEL : kind - K_L_LEN];
        memset(&cmv_ls, 0, sizeof cmv_ls);
        cu.lpage = (uint8_t)(kind >= K_L_LEVEL);
        cu_loop_cells(&cmv_ls);
        cu.lpage = was;
        cmv_cpy(d, c->value, n);
        if (c->glyph == CR_G_BAR)
            *pct = (int32_t)c->pct * 1000 / 255;
        return;
    }
    default:                                       /* a step's (cu_le, synced by the turn / the press) */
        if (kind == K_S_STEPS) {
            cmv_int(d, (int32_t)cu_le.nstep, 0, n);
            v = (int32_t)cu_le.nstep, lo = 1, hi = 64;
            break;
        }
        if (!cu_le.cnt) {
            cmv_cpy(d, "-", n);
            return;
        }
        switch (kind) {
        case K_S_LEN:
            if (cu_le.nf)
                cmv_cpy(d, CU_LE_FRAC[cu_le.nf & 3u], n);
            else {
                cmv_int(d, cu_le.n, 0, n);
                cmv_cat(d, cu_le.n == 1u ? " step" : " steps", n);
            }
            return;
        case K_S_CHANCE: cmv_int(d, CRL_CHANCE_PCT(cu_le.chance < 0 ? 0 : cu_le.chance), 0, n); cmv_cat(d, "%", n); return;
        case K_S_VEL: v = cu_le.vel, lo = 1, hi = 127; cmv_int(d, v, 0, n); break;
        case K_S_RAT: cmv_cpy(d, CU_LE_RAT[cu_le.rat < 0 ? 0 : cu_le.rat & 3], n); return;
        case K_S_COND: cmv_cpy(d, CU_LE_COND[cu_le.cond < 0 ? 0 : cu_le.cond % (int)CRL_NCOND], n); return;
        case K_S_VOIC: cu_le_voic_txt(d, n); return;
        case K_S_TR: {
            char nb[5];
            cr_note_name(cu_le.root, nb);
            cmv_cpy(d, nb, n);
            return;
        }
        default: cmv_cpy(d, "-", n); return;
        }
        break;
    }
    if (hi > lo)
        *pct = (v - lo) * 1000 / (hi - lo);
}
/* a knob's name and value, as its popup shows them (0: the knob has no job here) */
static int cmv_kv(int kind, char *t, uint32_t tn, char *v, uint32_t vn, int32_t *pct)
{
    if (kind >= K_LAY0)
        return cmv_lay_cell((uint32_t)(kind - K_LAY0), t, tn, v, vn, pct);
    cmv_cpy(t, CMV_KNAME[kind], tn);
    cmv_value(kind, v, vn, pct);
    return 1;
}
static void cmv_pop_say(int kind)
{
    char t[20], v[24], s[64], w[48];
    int32_t pct;
    if (!cmv_kv(kind, t, sizeof t, v, sizeof v, &pct))
        return;
    cmv_cpy(s, t, sizeof s);
    cmv_cat(s, " ", sizeof s);
    cmv_vwords(v, w, sizeof w);
    cmv_cat(s, w, sizeof s);
    cmv_say(s);
}
/* a knob that picks from a list (Perform, Key, Scale; a step's Chance, Ratchet, Condition; the looper's choices; a
 * settings knob whose value is a name): an entry every CMV_LIST_TICKS of the Move's encoder (the owner, 2026-11-20:
 * one an encoder tick was too fast); the rest a step a tick */
#define CMV_LIST_TICKS 4
static int cmv_listy(int kind)
{
    if (kind >= K_LAY0) {
        char t[20], v[24];
        int32_t pct;
        int c;
        if (!cmv_kv(kind, t, sizeof t, v, sizeof v, &pct))
            return 0;
        c = v[0] == '+' || v[0] == '-' ? v[1] : v[0];
        return !(c >= '0' && c <= '9');
    }
    return kind == K_PERF || kind == K_KEY || kind == K_SCALE || kind == K_S_CHANCE || kind == K_S_RAT ||
           kind == K_S_COND || kind == K_L_LEN || kind == K_L_RATE || kind == K_L_QUANT || kind == K_L_CIN ||
           kind == K_L_BEAT || kind == K_L_MODE;
}
static void cmv_knob(uint32_t k, int32_t d)
{
    static int8_t acc[8];
    static int8_t acc_kind[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    int kind = cmv_knob_kind(k);
    char t[20], v[24];
    int32_t pct;
    if (!d || k >= 8)
        return;
    if (cmv.loop)
        cmv.loop_used = 1;
    if (!cmv_kv(kind, t, sizeof t, v, sizeof v, &pct))   /* (no job here: a dark ring, nothing) */
        return;
    if (cmv_listy(kind) || kind == K_RATE) {      /* a list: 4 ticks an entry; Perf amount: 3 a step (proposal 5:
                                                    * a tick a step was too twitchy) */
        const int32_t per = kind == K_RATE ? CMV_RATE_TICKS : CMV_LIST_TICKS;
        int32_t t;
        if (acc_kind[k] != kind || (acc[k] > 0 && d < 0) || (acc[k] < 0 && d > 0))
            acc[k] = 0;                            /* another knob's job, or a turn back: start again */
        acc_kind[k] = (int8_t)kind;
        t = acc[k] + d;
        d = t / per;
        acc[k] = (int8_t)(t - d * per);
        if (!d) {
            cmv.pop_kind = (int8_t)kind;           /* the popup stays up while it gathers ticks */
            cmv.pop_until = cmv_now() + CMV_POP_MS;
            return;
        }
        for (; d; d -= d > 0 ? 1 : -1)             /* an entry at a time (Perform's turn takes the sign only) */
            cmv_turn(kind, d > 0 ? 1 : -1);
    } else
        cmv_turn(kind, d);
    cmv.pop_kind = (int8_t)kind;
    cmv.pop_until = cmv_now() + CMV_POP_MS;
    cmv.msg_until = 0;
    cmv.wheel_until = 0;
    cmv_mirror_msg();
    if ((int32_t)(cmv.msg_until - cmv_now()) <= 0)
        cmv_pop_say(kind);
}
static void cmv_knob_touch(uint32_t k, int on)
{
    int kind = cmv_knob_kind(k);
    char t[20], v[24];
    int32_t pct;
    if (on && !cmv_kv(kind, t, sizeof t, v, sizeof v, &pct))
        return;
    cmv.pop_touch = (uint8_t)(on ? cmv.pop_touch | (1u << k) : cmv.pop_touch & ~(1u << k));
    if (on) {
        if (kind >= K_S_LEN && kind <= K_S_STEPS) {
            cu_le.ok = 0;
            cu_le_sync();
        }
        cmv.pop_kind = (int8_t)kind;
        cmv.msg_until = 0;
        cmv_pop_say(kind);
    }
    cmv.pop_until = cmv_now() + CMV_POP_MS;
}

/* ------------------------------------------------------------ the input --- */
static void cmv_picker_pad(int r, int c)           /* Menu held: layouts, banks, progressions */
{
    char s[40], nm[CRPG_NAME + 1], v[8];
    cmv.menu_used = 1;
    if (r == 3 && c < 4) {
        cmv_set_layout((uint32_t)c);
    } else if (r == 2 && c < 2) {                  /* (the second row: ISO, ISO in key) */
        cmv_set_layout(c ? CMV_ISOK : CMV_ISO);
    } else if (r >= 2 && c >= 4) {
        cmv.bank = (uint8_t)((3 - r) * 4 + (c - 4));
        cmv.pads_dirty = 1;
        cmv_cpy(s, "Bank ", sizeof s);
        cmv_int(v, cmv.bank + 1, 0, sizeof v);
        cmv_cat(s, v, sizeof s);
        cmv_say(s);
        cu_trace("move: bank %u\n", (unsigned)cmv.bank + 1u);
    } else if (r <= 1) {
        uint32_t p = r == 1 ? (uint32_t)c : 8u + (uint32_t)c;
        cs.prog_sel = (uint8_t)p;
        cu_prog_fit();
        cmv.prog_page = 0;
        cmv.prog_last = -1;
        cmv_set_layout(CMV_PROG);
        crp_name(p, nm);
        cmv_cpy(s, "Progression ", sizeof s);
        cmv_int(v, (int32_t)p + 1, 0, sizeof v);
        cmv_cat(s, v, sizeof s);
        cmv_card(s, nm[0] ? nm : (crp_count(p) ? "-" : "empty"));   /* (proposal 6: its name shown, over the picker) */
        if (nm[0]) {
            cmv_cat(s, ", ", sizeof s);
            cmv_cat(s, nm, sizeof s);
        }
        cmv_say(s);
        cu_trace("move: progression %u\n", (unsigned)p + 1u);
    }
}
/* ------------------------------------------------- proposal 5: velocity, Settings, the clipboard --- */
static uint32_t cmv_vel(uint32_t v)                /* a pad's velocity, as the Settings say (RemoteFM14Move's curves) */
{
    v = v < 1u ? 1u : v > 127u ? 127u : v;
    switch (cmv.vmode) {
    case CMV_V_FIXED: return cmv.vfixed;
    case CMV_V_SOFT: return 127u - (127u - v) * (127u - v) / 127u;
    case CMV_V_HARD: v = v * v / 127u; return v ? v : 1u;
    default: return v;
    }
}
/* the lists (RemoteFM1's own Settings menu, seq_ui.c GLOBAL): the wheel the row, a click edits, the wheel the value,
 * a click (or Back) done. Shift + S2 the Settings: the pads' velocity (the Move's own), then the FM-1's Options that
 * mean something here (opt_get / opt_set / opt_text). Shift + S7 the Loop (the owner's "track settings", 2026-11-20):
 * the loop's swing and steps, then the PLAY menu's own (cmv_kv / cmv_turn); Shift + S3..S6 jump into it, editing */
enum { CMV_L_NONE, CMV_L_SET, CMV_L_LOOP };
#define CMV_S_VMODE (-1)
#define CMV_S_VFIX (-2)
#define CMV_S_SWING (-3)
#define CMV_S_STEPS (-4)
#define CMV_S_K 100                                /* + a knob kind: its name and value, its turn */
static const int16_t CMV_SETL[] = {CMV_S_VMODE, CMV_S_VFIX, O_STYLE, O_EXTADD, O_SECRET, O_SUSWIN, O_FULLSHORT, O_SIXTH,
                                   O_BASSMODE, O_SINGLE, O_SPLIT, O_CH_MAIN, O_CH_BASS, O_CLOCK, O_MIDI_CC};
static const int16_t CMV_LOOPL[] = {CMV_S_SWING, CMV_S_K + K_L_QUANT, CMV_S_K + K_L_RATE, CMV_S_STEPS,
                                    CMV_S_K + K_L_LEN, CMV_S_K + K_L_CIN, CMV_S_K + K_L_BEAT, CMV_S_K + K_L_LEVEL,
                                    CMV_S_K + K_L_MODE, CMV_S_K + K_TEMPO};
enum { CMV_LR_SWING, CMV_LR_QUANT, CMV_LR_RATE, CMV_LR_STEPS };   /* (the rows Shift + S3..S6 jump to) */
static const int16_t *cmv_list(uint32_t *n)
{
    if (cmv.set_open == CMV_L_LOOP) {
        *n = sizeof CMV_LOOPL / sizeof CMV_LOOPL[0];
        return CMV_LOOPL;
    }
    *n = sizeof CMV_SETL / sizeof CMV_SETL[0];
    return CMV_SETL;
}
static void cmv_set_row(uint32_t i, char *t, uint32_t tn, char *v, uint32_t vn)
{
    uint32_t n;
    const int16_t *L = cmv_list(&n);
    const int o = L[i % n];
    int32_t pct;
    if (o == CMV_S_VMODE) {
        cmv_cpy(t, "Velocity", tn);
        cmv_cpy(v, CMV_VNAME[cmv.vmode % CMV_V_N], vn);
    } else if (o == CMV_S_VFIX) {
        cmv_cpy(t, "Fixed level", tn);
        cmv_int(v, cmv.vfixed, 0, vn);
    } else if (o == CMV_S_SWING) {
        cmv_cpy(t, "Swing", tn);
        cmv_int(v, crl_swing, 0, vn);
        cmv_cat(v, "%", vn);
    } else if (o == CMV_S_STEPS) {
        cmv_cpy(t, "Steps", tn);
        cmv_int(v, (int32_t)cmv_nsteps(), 0, vn);
    } else if (o >= CMV_S_K) {
        if (!cmv_kv(o - CMV_S_K, t, tn, v, vn, &pct))
            cmv_cpy(v, "-", vn);
        if ((o - CMV_S_K) == K_L_BEAT)
            cmv_cpy(t, "Metronome", tn);
    } else {
        cmv_cpy(t, O_NAME[o], tn);
        opt_text((uint32_t)o, v, vn);
    }
}
static void cmv_set_say(const char *pre)
{
    char s[64], t[24], v[24], w[40];
    cmv_set_row(cmv.set_sel, t, sizeof t, v, sizeof v);
    cmv_cpy(s, pre, sizeof s);
    cmv_cat(s, t, sizeof s);
    cmv_cat(s, ": ", sizeof s);
    cmv_vwords(v, w, sizeof w);
    cmv_cat(s, w, sizeof s);
    cmv_say(s);
}
static void cmv_steps_turn(int32_t d)              /* the loop's length in steps (a held step's Steps knob) */
{
    int opened = 0;
    if (!cu.ledit) {
        cu_ledit_open();
        if (!cu.ledit) {
            cmv_mirror_msg();
            return;
        }
        opened = 1;
    }
    cu.le_page = 0;
    cu.le_cell = 0;
    cu_le.ok = 0;
    cu_le_sync();
    cu_ledit_knob(1, d);
    cu_le.ok = 0;
    cu_le_sync();
    if (opened)
        cu_ledit_close();
    cmv.map_ok = 0;
}
static void cmv_set_turn(int32_t d)                /* the wheel in a list */
{
    uint32_t n;
    const int16_t *L = cmv_list(&n);
    if (!cmv.set_edit) {
        int32_t r = (int32_t)cmv.set_sel + d;
        cmv.set_sel = (uint8_t)(r < 0 ? 0 : r >= (int32_t)n ? (int32_t)n - 1 : r);
        cmv_set_say("");
        return;
    }
    {
        const int o = L[cmv.set_sel % n];
        if (o == CMV_S_VMODE) {
            int32_t m = (int32_t)cmv.vmode + (d > 0 ? 1 : -1);
            cmv.vmode = (uint8_t)(m < 0 ? 0 : m >= CMV_V_N ? CMV_V_N - 1 : m);
        } else if (o == CMV_S_VFIX) {
            int32_t f = (int32_t)cmv.vfixed + d;
            cmv.vfixed = (uint8_t)(f < 1 ? 1 : f > 127 ? 127 : f);
        } else if (o == CMV_S_SWING) {
            int32_t w = (int32_t)crl_swing + d;
            crl_swing = (uint8_t)(w < 50 ? 50 : w > 75 ? 75 : w);
        } else if (o == CMV_S_STEPS)
            cmv_steps_turn(d);
        else if (o >= CMV_S_K)
            cmv_turn(o - CMV_S_K, d > 0 ? 1 : -1);
        else
            opt_set((uint32_t)o, opt_get((uint32_t)o) + (d > 0 ? 1 : -1));   /* (applied at once: Back keeps it) */
        cmv_set_say("");
    }
}
static void cmv_list_open(uint32_t list, int row, int edit)   /* a list opened (row: -1 where it was); again: home */
{
    uint32_t n;
    if (cmv.set_open == list && (row < 0 || cmv.set_sel == (uint32_t)row)) {
        cmv.set_open = cmv.set_edit = 0;
        cmv_say("Home");
        return;
    }
    if (cmv_opts())
        cu_close_all();
    if (cmv_layer())
        cu_layer_close();
    if (cmv_editing())
        ce_close();
    if (cmv.set_open != list)
        cmv.set_sel = 0;
    cmv.set_open = (uint8_t)list;
    cmv_list(&n);
    if (row >= 0)
        cmv.set_sel = (uint8_t)row;
    if (cmv.set_sel >= n)
        cmv.set_sel = 0;
    cmv.set_edit = (uint8_t)(edit != 0);
    cmv.pop_kind = -1;
    cmv.msg_until = cmv.wheel_until = 0;
    cmv.wheel_touch = 0;
    cmv_set_say(list == CMV_L_LOOP ? (edit ? "Loop, editing " : "Loop, ") : "Settings, ");
}
static void cmv_set_toggle(void) { cmv_list_open(CMV_L_SET, -1, 0); }   /* Shift + S2 */
static void cmv_tap_tempo(void)                    /* Shift + S9: the tempo from the taps (two or more, 0.25 .. 2 s apart) */
{
    static uint32_t t[4];
    static uint8_t n;
    const uint32_t now = cmv_now();
    char s[24], v[8];
    if (n && now - t[(n - 1u) & 3u] > 2000u)
        n = 0;
    t[n & 3u] = now;
    n++;
    if (n >= 2u) {
        const uint32_t k = n > 4u ? 4u : n, first = t[(n - k) & 3u], span = now - first;
        const int32_t bpm = span ? (int32_t)(60000u * (k - 1u) / span) : 0;
        if (bpm >= 20 && bpm <= 300 && !(cr_route.clock_in && crl.ext_on)) {
            cu_set_tempo(bpm);
            cmv_int(v, bpm, 0, sizeof v);
            cmv_cpy(s, v, sizeof s);
            cmv_cat(s, " bpm", sizeof s);
            cmv_msg("Tempo", s);
            return;
        }
    }
    cmv_say("Tap");
}
static int cmv_sstep_on(uint32_t i)                /* a Shift + step's state: its step lit (dim: off) */
{
    switch (i) {
    case CMV_S_VEL: return cmv.vmode == CMV_V_FIXED;
    case CMV_S_SET: return cmv.set_open == CMV_L_SET;
    case CMV_S_SWG: return crl_swing > 50u;
    case CMV_S_QNT: return cs.loop_quant % CRL_NQUANT != 0u;
    case CMV_S_RATE: return cmv.set_open == CMV_L_LOOP && cmv.set_sel == CMV_LR_RATE;
    case CMV_S_STP: return cmv.set_open == CMV_L_LOOP && cmv.set_sel == CMV_LR_STEPS;
    case CMV_S_LOOP: return cmv.set_open == CMV_L_LOOP;
    case CMV_S_EDIT: return cmv_editing();
    case CMV_S_RECM: return cs.loop_mode % CRL_NMODE != 0u;
    case CMV_S_CIN: return cs.loop_count_in;
    case CMV_S_MET: return cs.metro;
    default: return 0;
    }
}
static void cmv_shift_step(uint32_t i)             /* Shift + a step (proposal 6) */
{
    char s[40];
    switch (i) {
    case CMV_S_VEL:                                /* Fixed velocity on / off (its level: the Settings) */
        cmv.vmode = (uint8_t)(cmv.vmode == CMV_V_FIXED ? CMV_V_DYN : CMV_V_FIXED);
        cmv_say(cmv.vmode == CMV_V_FIXED ? "Fixed velocity, on" : "Fixed velocity, off");
        return;
    case CMV_S_SET: cmv_set_toggle(); return;
    case CMV_S_SWG: cmv_list_open(CMV_L_LOOP, CMV_LR_SWING, 1); return;
    case CMV_S_QNT: cmv_list_open(CMV_L_LOOP, CMV_LR_QUANT, 1); return;
    case CMV_S_RATE: cmv_list_open(CMV_L_LOOP, CMV_LR_RATE, 1); return;
    case CMV_S_STP: cmv_list_open(CMV_L_LOOP, CMV_LR_STEPS, 1); return;
    case CMV_S_LOOP: cmv_list_open(CMV_L_LOOP, -1, 0); return;
    case CMV_S_EDIT: cmv_edit_toggle(0); return;
    case CMV_S_TAP: cmv_tap_tempo(); return;
    case CMV_S_RECM:
        cu_rec_mode_next();
        cmv_msg("Rec mode", CU_LOOP_MODE[cs.loop_mode % CRL_NMODE]);
        return;
    case CMV_S_CIN:
        cmv_turn(K_L_CIN, cs.loop_count_in ? -1 : 1);
        cmv_cpy(s, cs.loop_count_in ? "Count-in, on" : "Count-in, off", sizeof s);
        cmv_say(s);
        return;
    case CMV_S_MET:
        cu_metro_set(!cs.metro);
        cmv_say(cs.metro ? "Metronome, on" : "Metronome, off");
        return;
    default:
        return;
    }
}

/* the clipboard (RemoteFM14Move's seq_ui.c, for chord steps): Copy held + a step with a chord: copied (more in the same
 * hold join, gaps kept, from the first); + an empty step: pasted. Copy let go unused by a paste: the next plain step tap
 * pastes once. Delete held + a step: cut; right after a cut any step pastes (replacing it); a step after a paste is cut
 * again. The marks blink: white copied, red cut */
#define CMV_CLIP_N 16u
#define CMV_CLIP_EV 4u
typedef struct {
    uint8_t off, ne, nlen, nfrac;
    int8_t prop[4];
    uint8_t root[CMV_CLIP_EV], qx[CMV_CLIP_EV], vel[CMV_CLIP_EV];
} cmv_clip_t;
static cmv_clip_t cmv_clip[CMV_CLIP_N];
static uint8_t cmv_nclip, cmv_clip0;               /* steps held; the first one's absolute step */
static void cmv_clip_cursor(uint32_t s)
{
    cu.le_cell = (uint16_t)s;
    cu_le.ok = 0;
    cu_le_sync();
}
static int cmv_clip_take(uint32_t s, int join)     /* step s into the clipboard; 0: it holds nothing */
{
    cmv_clip_t *c;
    uint32_t i, t0 = s * CRL_STEP, t1 = t0 + CRL_STEP;
    cmv_clip_cursor(s);
    if (!cu_le.cnt)
        return 0;
    if (!join || !cmv_nclip || s < cmv_clip0 || s - cmv_clip0 > 255u || cmv_nclip >= CMV_CLIP_N) {
        cmv_nclip = 0;
        cmv_clip0 = (uint8_t)s;
        cmv.marks = 0;
    }
    c = &cmv_clip[cmv_nclip++];
    memset(c, 0, sizeof *c);
    c->off = (uint8_t)(s - cmv_clip0);
    fm1_irq_off();
    for (i = 0; i < crl.d.nev && c->ne < CMV_CLIP_EV; i++) {
        const crl_ev_t *e = &crl.d.ev[i];
        if ((e->layer & CRL_HID) || e->t < t0 || e->t >= t1)
            continue;
        c->root[c->ne] = e->root, c->qx[c->ne] = e->qx, c->vel[c->ne] = e->vel;
        c->ne++;
    }
    c->nlen = (uint8_t)cr_loop_ed_nlen(&crl);
    c->nfrac = (uint8_t)cr_loop_ed_nfrac(&crl);
    for (i = 0; i < 4u; i++)
        c->prop[i] = (int8_t)cr_loop_ed_prop(&crl, (int)i);
    fm1_irq_on();
    if (s < 64u)
        cmv.marks |= (uint64_t)1u << s;
    return 1;
}
static void cmv_clip_paste(uint32_t s)             /* the clipboard from step s on (what is there replaced) */
{
    uint32_t k, j, i;
    for (k = 0; k < cmv_nclip; k++) {
        const cmv_clip_t *c = &cmv_clip[k];
        uint32_t at = s + c->off;
        if (at >= CRL_STEPS)
            break;
        cmv_clip_cursor(at);
        fm1_irq_off();
        cr_loop_ed_clear(&crl);
        for (j = 0; j < c->ne; j++) {
            cr_loop_gesture(&crl, CMV_GID, c->root[j], (uint8_t)(c->qx[j] >> 4), (uint8_t)(c->qx[j] & 15u), c->vel[j], 1);
            cr_loop_gesture(&crl, CMV_GID, c->root[j], (uint8_t)(c->qx[j] >> 4), (uint8_t)(c->qx[j] & 15u), c->vel[j], 0);
        }
        fm1_irq_on();
        cmv_clip_cursor(at);
        fm1_irq_off();
        if (c->nfrac)
            cr_loop_ed_setnfrac(&crl, c->nfrac);
        else if (c->nlen > 1u)
            cr_loop_ed_setnlen(&crl, c->nlen);
        for (i = 0; i < 4u; i++)
            if (c->prop[i] >= 0)
                cr_loop_ed_setprop(&crl, (int)i, c->prop[i]);
        fm1_irq_on();
    }
    cmv_clip_cursor(s);
    cmv.marks = 0;
    cmv.map_ok = 0;
}
static void cmv_clip_say(const char *a, uint32_t s, const char *b)
{
    char t[40], v[8];
    cmv_cpy(t, a, sizeof t);
    cmv_int(v, (int32_t)s + 1, 0, sizeof v);
    cmv_cat(t, v, sizeof t);
    cmv_cat(t, b, sizeof t);
    cmv_say(t);
}
/* a step pressed with Copy or Delete held, or a paste armed: 1 = handled (no tap, no preview) */
static int cmv_clip_step(uint32_t s)
{
    if (cmv.copy) {
        cmv.copy_used = 1;
        if (cmv_clip_take(s, !cmv.mark_cut && cmv.marks)) {
            cmv.mark_cut = 0;
            cmv.paste_armed = 0;
            cmv_clip_say("Step ", s, " copied");
        } else if (cmv_nclip) {
            cmv.copy_used = 2;                     /* (pasted in this hold: nothing armed at the release) */
            cmv_clip_paste(s);
            cmv_clip_say("Pasted to step ", s, "");
        }
        return 1;
    }
    if (cmv.del) {
        cmv.del_used = 1;
        cmv_clip_cursor(s);
        if (cmv.del_last != 1u && cu_le.cnt) {     /* a cut: copied, then cleared */
            cmv_clip_take(s, 0);
            cmv.mark_cut = 1;
            cmv_clip_cursor(s);
            fm1_irq_off();
            cr_loop_ed_clear(&crl);
            fm1_irq_on();
            cmv.del_last = 1;
            cmv.map_ok = 0;
            cmv_clip_say("Step ", s, " cut");
        } else if (cmv_nclip) {
            int had = cu_le.cnt != 0;
            cmv_clip_paste(s);
            cmv.del_last = 2;
            cmv.mark_cut = 0;
            had ? cmv_clip_say("Step ", s, " replaced") : cmv_clip_say("Pasted to step ", s, "");
        } else
            cmv_clip_say("Step ", s, ", empty");
        return 1;
    }
    if (cmv.paste_armed && !cmv.shift && !cmv.loop) {   /* Copy let go after copying: this tap pastes, once */
        cmv.paste_armed = 0;
        cmv_clip_paste(s);
        cmv_clip_say("Pasted to step ", s, "");
        return 1;
    }
    return 0;
}
static void cmv_loop_dup(void)                     /* Copy tapped alone: the loop into the next empty slot */
{
    uint32_t k, n;
    char t[24], v[8];
    if (!crl.d.nev) {
        cmv_msg("Copy", "the loop is empty");
        return;
    }
    if (cu_playing()) {
        cmv_msg("Copy", "stop first");
        return;
    }
    for (k = 1; k < CRL_SLOTS; k++)
        if (!((cs.loop_used >> ((cs.loop_slot + k) % CRL_SLOTS)) & 1u))
            break;
    if (k >= CRL_SLOTS) {
        cmv_msg("Copy", "no empty slot");
        return;
    }
    k = (cs.loop_slot + k) % CRL_SLOTS;
    if (cu_loop_flush("a copy") || !crl_fl_ready()) {
        cmv_msg("Copy", "not saved");
        return;
    }
    fm1_irq_off();
    n = cr_loop_pack(&crl.d, cu_loop_pend, sizeof cu_loop_pend);
    fm1_irq_on();
    if (!n || crl_fl_save(k, cu_loop_pend, n)) {
        cmv_msg("Copy", "not saved");
        return;
    }
    cs.loop_used |= (uint16_t)(1u << k);
    cmv_cpy(t, "slot ", sizeof t);
    cmv_int(v, (int32_t)k + 1, 0, sizeof v);
    cmv_cat(t, v, sizeof t);
    cmv_msg("Loop copied", t);
    cu_trace("move: loop copied to slot %u (%u bytes)\n", (unsigned)k + 1u, (unsigned)n);
}
static void cmv_copy_button(int on)
{
    if (on) {
        cmv.copy = 1;
        cmv.copy_used = 0;
        return;
    }
    if (!cmv.copy)
        return;
    cmv.copy = 0;
    if (cmv.copy_used == 1u) {                     /* copied, no paste yet: the next step tap pastes */
        cmv.paste_armed = 1;
        cmv_say("Tap a step to paste");
    } else if (!cmv.copy_used) {                   /* tapped alone: a paste waiting dropped, else the loop duplicated */
        if (cmv.paste_armed || cmv.marks) {
            cmv.paste_armed = 0;
            cmv.marks = 0;
            cmv.mark_cut = 0;
            cmv_say("Paste dropped");
        } else
            cmv_loop_dup();
    }
}
static void cmv_clip_del_up(void) { cmv.del_last = 0; }

/* Shift held: the eight FM-1 buttons on the pads of columns 1-2 (the screen's 2 x 4 grid, pad for name, proposal 6):
 * shortcut i at the top row's pair first */
static int cmv_sc_pad(int r, int c) { return c < 2 ? (3 - r) * 2 + c : -1; }   /* (proposal 6: the left side) */
static void cmv_pad_down(uint32_t n, uint32_t vel)
{
    int r = (int)(n / 8u), c = (int)(n % 8u);
    cmv.pad_held |= 1u << n;
    cmv.pad_kind[n] = PK_NONE;
    cu.last_input = cmv_now();
    vel = cmv_vel(vel);
    if (cmv.step >= 0)
        cmv.step_used = 1;
    if (cmv.menu) {                                /* the picker */
        cmv.pad_kind[n] = PK_PICK;
        cmv_picker_pad(r, c);
        return;
    }
    if (cmv.shift && cmv_sc_pad(r, c) >= 0) {      /* Shift + a pad of columns 7-8: an FM-1 button (tap; held: its
                                                    * settings, cmv_frame) */
        cmv.shift_used = 1;
        cmv.pad_kind[n] = PK_SC;
        cmv.pad_arg[n] = (uint8_t)cmv_sc_pad(r, c);
        cmv.sc = (uint8_t)(cmv.pad_arg[n] + 1u);
        cmv.sc_t0 = cmv_now();
        cmv.sc_open = 0;
        return;
    }
    if (cmv.shift && !(r >= 2 && c >= 4)) {        /* Shift: only the shortcuts and the top right (memories, or the
                                                    * editor's groups) answer (proposal 6) */
        cmv.shift_used = 1;
        return;
    }
    if (cmv_editing() && r >= 2 && c >= 4) {       /* the editor: the memories' pads are its groups */
        cmv_ed_group((uint32_t)((3 - r) * 4 + (c - 4)));
        return;
    }
    if (r >= 2 && c < 4 && !cmv_iso()) {         /* a chord key (LOCK: its Shift pad) */
        uint32_t j = (uint32_t)((3 - r) * 4 + c);
        cmv.pad_kind[n] = PK_MOD;
        cmv.pad_arg[n] = (uint8_t)j;
        cmv_mod(j, 1);
        return;
    }
    if (r >= 2 && (!cmv_iso() || cmv.shift)) {    /* a memory (ISO: with Shift held) */
        uint32_t j = (uint32_t)((3 - r) * 4 + (c - 4));
        if (cmv.pads_dirty)
            cmv_pads_sync();
        if (cmv.del) {
            cmv.del_used = 1;
            cmv_mem_clear(j);
            return;
        }
        if (cmv.keys_down && cmv.step < 0) {       /* a key held: what it plays is saved here */
            cmv_mem_save(j, (int)n);
            return;
        }
        if (!cmv.mem[cmv.bank * 8u + j].used) {
            cmv_msg("Memory", "empty");
            return;
        }
        cmv.pad_kind[n] = PK_MEM;
        cmv.pad_arg[n] = (uint8_t)j;
        cmv_mem_play(j, 1);
        {
            char nm[16], s[48], w[40], v[8];
            cmv_mem_name(&cmv.mem[cmv.bank * 8u + j], nm, sizeof nm);
            cmv_cpy(s, "Memory ", sizeof s);
            cmv_int(v, (int32_t)(cmv.bank * 8u + j) + 1, 0, sizeof v);
            cmv_cat(s, v, sizeof s);
            cmv_cat(s, ", ", sizeof s);
            cmv_words(nm, w, sizeof w);
            cmv_cat(s, w, sizeof s);
            cmv_say(s);
            cmv_cpy(cmv.said_chord, nm, sizeof cmv.said_chord);   /* (the chord it sounds: said with it, not again) */
        }
        return;
    }
    if (cmv.layout == CMV_PROG) {                  /* a progression's chord */
        uint32_t i = cmv.prog_page * 16u + (uint32_t)(r * 8 + c);
        uint8_t nt[CRPG_NOTES];
        uint32_t k = i < CRPG_KEYS ? crp_chord(cs.prog_sel, i, nt) : 0u;
        cmv.prog_last = (int8_t)i;
        if (cu_ckeys_used()) {                     /* a chord key in use: that type on the chord's lowest note */
            if (!k)
                return;
            cmv.pad_kind[n] = PK_ROOT;
            cmv.pad_arg[n] = nt[0];
            cmv.keys_down++;
            cu_mroot(nt[0], vel, 1);
            return;
        }
        if (!k)
            return;
        cmv.pad_kind[n] = PK_PTRIG;
        cmv.pad_arg[n] = (uint8_t)(CMV_TRIG0 + (uint32_t)(r * 8 + c));
        cmv.keys_down++;
        vel = CR_PROG_VEL(vel, cu_pvel());
        cu.last_sound = cmv_now();
        cr_post(CRE_PKEY, cmv.pad_arg[n], vel, (int32_t)(1u | i << 1 | (uint32_t)cs.prog_sel << 7));
        return;
    }
    {
        int note = cmv_pad_note(r, c);
        if (note < 0 || note > 127)
            return;
        cmv.pad_kind[n] = PK_ROOT;
        cmv.pad_arg[n] = (uint8_t)note;
        cmv.keys_down++;
        cu_mroot((uint32_t)note, vel, 1);
    }
}
static void cmv_pad_up(uint32_t n)
{
    uint32_t k = cmv.pad_kind[n];
    cmv.pad_held &= ~(1u << n);
    cmv.pad_kind[n] = PK_NONE;
    switch (k) {
    case PK_ROOT:
        cu_mroot(cmv.pad_arg[n], 0, 0);
        cmv.keys_down = (uint8_t)(cmv.keys_down ? cmv.keys_down - 1u : 0u);
        break;
    case PK_PTRIG:
        cr_post(CRE_PKEY, cmv.pad_arg[n], 0, 0);
        cmv.keys_down = (uint8_t)(cmv.keys_down ? cmv.keys_down - 1u : 0u);
        break;
    case PK_MEM: cmv_mem_play(cmv.pad_arg[n], 0); break;
    case PK_MOD: cmv_mod(cmv.pad_arg[n], 0); break;
    case PK_SC:
        if (cmv.sc == cmv.pad_arg[n] + 1u) {
            if (!cmv.sc_open)
                cmv_sc_tap(cmv.pad_arg[n]);
            cmv.sc = 0;
        }
        break;
    default: break;
    }
    if (!cmv.keys_down && cmv.at_last) {           /* (the pressure ends with the last key) */
        cmv.at_last = 0;
        cr_post(CRE_AT, 0, 0, 0);
    }
}
static void cmv_page(int d)
{
    char s[16], v[4];
    if (cmv.shift && cmv.layout == CMV_PROG) {     /* Shift + Left / Right: the Prog layout's chords 17-32, 33-48 */
        int p = (int)cmv.prog_page + d;
        cmv.shift_used = 1;
        cmv.prog_page = (uint8_t)(p < 0 ? 0 : p > 2 ? 2 : p);
        cmv_cpy(s, "Chords ", sizeof s);
        cmv_int(v, cmv.prog_page * 16 + 1, 0, sizeof v);
        cmv_cat(s, v, sizeof s);
        cmv_msg(s, "");
        return;
    }
    {
        int p = (int)cmv.page + d;
        cmv.page = (uint8_t)(p < 0 ? 0 : p > 3 ? 3 : p);
        cmv_cpy(s, "Page ", sizeof s);
        cmv_int(v, cmv.page + 1, 0, sizeof v);
        cmv_cat(s, v, sizeof s);
        cmv_say(s);
    }
}
static void cmv_bass_tap(void)                     /* Mute: the bass on / off (cu_tap's BT_BASS) */
{
    if (!cs.bass_on && !cs.bass_sound) {
        cs.bass_sound = (uint16_t)(cu_part_pos(1) + 1u);
        cs.bass_on = 1;
        cr_post(CRE_BASS, 0, 0, 1);
    } else {
        cs.bass_on ^= 1u;
        cr_post(CRE_BASS, 0, 0, cs.bass_on);
    }
    cmv_msg("Bass", cs.bass_on ? "on" : "off");
}
/* Shift + a step (proposal 4, C): the FM-1's buttons. A tap: the on / off (as the button's tap); held: its settings */
static int cmv_sc_on(uint32_t i)
{
    switch (i) {
    case SC_KEY: return cs.key_on;
    case SC_LOCK: return cu.clock;
    case SC_LATCH: return cs.sticky;
    case SC_PERF: return cs.perform_on;
    case SC_BPERF: return !cu_pf_off(CU_PF_BASS);
    case SC_FX: return cs.fx_on;
    case SC_BASS: return cs.bass_on;
    case SC_BEAT: return cs.metro;
    default: return 0;
    }
}
static void cmv_sc_tap(uint32_t i)
{
    const char *nm = i < SC_N ? CMV_SC_SAY[i] : "";
    char t[40];
    switch (i) {
    case SC_KEY:                                   /* (cu_tap BT_KEY; SCALE no longer restores it on leaving) */
        cs.key_on ^= 1u;
        cu_post_key();
        cmv.scale_keyed = 0;
        break;
    case SC_LOCK: cu_lock_tap(); break;
    case SC_LATCH: cu_latch_toggle(); break;
    case SC_PERF: cu_perf_toggle(); break;
    case SC_BPERF: cu_perf_part_set(CU_PF_BASS, !cu_pf_off(CU_PF_BASS)); break;
    case SC_FX: cu_fx_toggle(); break;
    case SC_BASS: cmv_bass_tap(); cmv.msg_until = 0; return;   /* (said; the inverted name shows it: no popup) */
    case SC_BEAT: cu_metro_set(!cs.metro); break;
    default: return;
    }
    cmv_cpy(t, nm, sizeof t);                      /* (proposal 5: no popup, the Shift screen's inverted name says it) */
    cmv_cat(t, cmv_sc_on(i) ? ", on" : ", off", sizeof t);
    cmv_say(t);
    cmv.msg_until = 0;
    cu_trace("move: shortcut %s %s\n", nm, cmv_sc_on(i) ? "on" : "off");
}
static void cmv_sc_hold(uint32_t i)
{
    static const uint8_t L[SC_N] = {L_KEY, L_LOCK, 0, L_PERF, L_PERF, L_FX, L_BASS, L_METRO};
    uint32_t l = i < SC_N ? L[i] : 0u, part = i == SC_BPERF;
    if (!l) {                                      /* LATCH, the editors, the Options: as a tap */
        cmv_sc_tap(i);
        return;
    }
    if (cmv_layer() == l && (l != L_PERF || cu.perf_part == part)) {   /* held again: closed */
        cu_layer_close();
        cmv_say("Closed");
        return;
    }
    cmv_layer_open(l, part);
}
static void cmv_wheel_show(const char *t, const char *v)
{
    char s[48];
    cmv_cpy(cmv.wheel_t, t, sizeof cmv.wheel_t);
    cmv_cpy(cmv.wheel_v, v, sizeof cmv.wheel_v);
    cmv.wheel_until = cmv_now() + CMV_POP_MS;
    cmv.wheel_k = -1;
    cmv.pop_kind = -1;
    cmv.msg_until = 0;
    cmv_cpy(s, t, sizeof s);
    cmv_cat(s, " ", sizeof s);
    cmv_cat(s, v, sizeof s);
    cmv_say(s);
}
static void cmv_wheel_sound(void)                  /* what the wheel turns: the preset (its engine over it), the engine */
{
    char nm[13];
    if (cmv.wheel_eng) {
        cmv_wheel_show("Engine", ENGINES[trk[CR_PART_CHORD].eng_req % NENGINES]->name);
        cmv.wheel_k = 1;
        return;
    }
    pool_name(trk[CR_PART_CHORD].eng_req % NENGINES, cs.sound, nm);
    cmv_wheel_show(ENGINES[trk[CR_PART_CHORD].eng_req % NENGINES]->name, nm);
    cmv.wheel_k = 0;
}
static void cmv_wheel_bass(void)                   /* .. Shift: the bass's */
{
    char nm[13];
    if (cmv.wheel_beng) {
        cmv_wheel_show("Bass engine", ENGINES[trk[CR_PART_BASS].eng_req % NENGINES]->name);
        cmv.wheel_k = 3;
        return;
    }
    if (!cs.bass_sound || !cs.bass_on)
        cmv_cpy(nm, "Off", sizeof nm);
    else
        pool_name(trk[CR_PART_BASS].eng_req % NENGINES, cs.bass_sound - 1u, nm);
    cmv_wheel_show("Bass", nm);
    cmv.wheel_k = 2;
}
static void cmv_button(uint32_t cc, int on)
{
    char s[24];
    if (on) {
        cu.last_input = cmv_now();
        if (cmv.step >= 0 && cc != RFM1_TK_CC_SHIFT && cc != RFM1_TK_CC_DELETE)
            cmv.step_used = 1;
    }
    switch (cc) {
    case RFM1_TK_CC_SHIFT:
        cmv.shift = (uint8_t)on;
        if (on)
            cmv.shift_used = 0;
        cmv.pop_kind = -1;
        break;
    case RFM1_TK_CC_MENU:
        if (on) {
            cmv.menu = 1;
            cmv.menu_used = 0;
            cmv.menu_t0 = cmv_now();
        } else if (cmv.menu) {
            cmv.menu = 0;
            if (!cmv.menu_used && cmv_now() - cmv.menu_t0 < CMV_HOLD_MS)   /* a tap: the previous layout */
                cmv_set_layout(cmv.prev);
        }
        break;
    case RFM1_TK_CC_BACK:                          /* (the host gives Back at its release, press and release) */
        if (on) {
            cmv.pop_kind = -1;
            cmv.msg_until = cmv.saved_until = cmv.wheel_until = 0;
            cmv_home();                            /* (the Settings, a settings layer, the editor: all of them) */
        }
        break;
    case RFM1_TK_CC_CAPTURE:
        if (on) {
            cu_gsave();
            cmv_mirror_msg();
        }
        break;
    case RFM1_TK_CC_UP:
    case RFM1_TK_CC_DOWN:
        if (on) {
            int o = cu.octave + (cc == RFM1_TK_CC_UP ? 1 : -1);
            cu.octave = (int8_t)(o < -2 ? -2 : o > 2 ? 2 : o);
            cu_post_kbd();
            cmv_int(s, cu.octave, cu.octave != 0, sizeof s);
            cmv_msg("Octave", s);
        }
        break;
    case RFM1_TK_CC_UNDO:
        if (on) {
            cr_post(CRE_LOOP, LP_UNDO, 0, 0);
            cmv_msg("Undo", "");
        }
        break;
    case RFM1_TK_CC_LOOP:                          /* tapped: LATCH; held: the slots on the steps, the looper's knobs */
        if (on) {
            cmv.loop = 1;
            cmv.loop_used = 0;
            cmv.loop_t0 = cmv_now();
            cmv.pop_kind = -1;
        } else if (cmv.loop) {
            cmv.loop = 0;
            cmv.pop_kind = -1;
            if (!cmv.loop_used && cmv_now() - cmv.loop_t0 < CMV_HOLD_MS) {
                cu_latch_toggle();
                cmv_msg("Latch", cs.sticky ? "on" : "off");
            }
        }
        break;
    case RFM1_TK_CC_SAMPLE:                        /* the chord sound's favourite mark (proposal 4: off the wheel) */
        if (on) {
            int was = cu_fav_part(0);
            if (cu_fav_turn(0, was ? -1 : 1))
                cmv_wheel_show("Favorite", was ? "off" : "on");
        }
        break;
    case RFM1_TK_CC_COPY:                          /* the clipboard (proposal 5; LOCK: Shift + its pad, Shift + a chord key) */
        cmv_copy_button(on);
        break;
    case RFM1_TK_CC_LEFT:
    case RFM1_TK_CC_RIGHT:
        if (on)
            cmv_page(cc == RFM1_TK_CC_RIGHT ? 1 : -1);
        break;
    case RFM1_TK_CC_MUTE:
        if (on)
            cmv_bass_tap();
        break;
    case RFM1_TK_CC_DELETE:
        if (on) {
            cmv.del = 1;
            cmv.del_used = 0;
        } else if (cmv.del) {
            cmv.del = 0;
            if (!cmv.del_used) {                   /* tapped alone: PANIC with Shift (proposal 5), else a hint */
                if (cmv.shift) {
                    uint32_t n;
                    cmv.shift_used = 1;
                    for (n = 0; n < 32u; n++)      /* (what the pads hold ends nothing more) */
                        if (cmv.pad_kind[n] == PK_ROOT || cmv.pad_kind[n] == PK_PTRIG)
                            cmv.pad_kind[n] = PK_NONE;
                    cmv.keys_down = 0;
                    cu_mroot_allup();
                    cu_panic();
                    cmv_msg("PANIC", "all notes off");
                } else
                    cmv_say("Delete: hold and press a step or a memory");
            }
            cmv_clip_del_up();
        }
        break;
    case RFM1_TK_CC_PLAY:
        if (on && cmv.del) {                       /* Delete + Play: the loop cleared (Undo brings it back) */
            cmv.del_used = 1;
            cmv.ghost = 0;
            cr_post(CRE_LOOP, LP_CLEAR, 0, 0);
            cmv.marks = 0;
            cmv.map_ok = 0;
            cmv_msg("Loop", "cleared");
            break;
        }
        if (on) {
            if (cmv.ghost) {                       /* the empty bar: stopped */
                cmv.ghost = 0;
                break;
            }
            if (cmv.shift && cu_playing()) {       /* Shift + Play: from step 1 */
                cr_post(CRE_LOOP, LP_PLAY, 0, 0);
                cmv.shift_used = 1;
            }
            cr_post(CRE_LOOP, LP_PLAY, 0, 0);
        }
        break;
    case RFM1_TK_CC_REC:
        if (on) {
            if (cmv.shift) {
                cu_rec_mode_next();
                cmv.shift_used = 1;
                cmv_msg("Rec mode", CU_LOOP_MODE[cs.loop_mode % CRL_NMODE]);
            } else {
                cu_rec();
                cmv_mirror_msg();
            }
        }
        break;
    case RFM1_TK_CC_JOG_CLICK:                     /* the wheel's click: preset <-> engine (Shift: the bass's); the
                                                    * Options: a group opened */
        if (!on)
            break;
        if (cmv.set_open) {                        /* the Settings: a click edits the row, again: done */
            cmv.set_edit ^= 1u;
            cmv.set_edit ? cmv_set_say("Editing ") : cmv_say("Done");
        } else if (cmv_opts()) {
            if (!cu.opt_in && cu.opt_g != CU_G_RESET) {
                cu_opt_enter();
                cmv_say(CU_G_NAME[cu.opt_g]);
            }
        } else if (cmv_layer() || cmv_editing())
            break;
        else if (cmv.shift) {
            cmv.shift_used = 1;
            cmv.wheel_beng ^= 1u;
            cmv_wheel_bass();
        } else {
            cmv.wheel_eng ^= 1u;
            cmv_wheel_sound();
        }
        break;
    default:
        break;
    }
}
static void cmv_wheel(int32_t d)
{
    if (!d)
        return;
    if (cmv.set_open) {
        cmv_set_turn(d);
        cmv.wheel_until = 0;
        return;
    }
    if (cmv_opts() || cmv_layer()) {               /* a settings layer: its pages (the FM-1's SELECT) */
        cu_knob(EN_SELECT, d);
        cmv.wheel_until = 0;
        if (cmv_layer())
            cmv_layer_say();
        else {
            const cr_screen_t *s = cmv_lscreen();
            if (s->n_items)
                cmv_say(s->item[(uint32_t)(s->sel - s->item0) % CR_PICK_MAX]);
        }
        return;
    }
    if (cmv_editing()) {                           /* the editor: its lanes, across its screens */
        ce_select(d);
        cmv.wheel_until = 0;
        return;
    }
    if (cmv.shift) {                               /* Shift + the wheel: the bass sound (its engine after a click) */
        cmv.shift_used = 1;
        if (cmv.wheel_beng) {
            cu_epk_step(1, d);
            cu_epk_commit();
        } else
            cmv_turn(K_BSOUND, d);
        cmv_wheel_bass();
        return;
    }
    if (cmv.wheel_eng) {                           /* the chord part's engine (OPT + PRESETS) */
        cu_epk_step(0, d);
        cu_epk_commit();
    } else
        cmv_sound_turn(d);
    cmv_wheel_sound();
}
static void cmv_draw(void);
static void cmv_leds(void);
static void cmv_input(const uint8_t p[4])
{
    const uint32_t st = p[1] & 0xF0u, d = p[2], v = p[3];
    fm1_irq_off();
    if (st == 0x90u || st == 0x80u) {
        const int on = st == 0x90u && v;
        if (d >= RFM1_TK_NOTE_PAD0 && d < RFM1_TK_NOTE_PAD0 + 32u) {
            if (on && !((cmv.pad_held >> (d - RFM1_TK_NOTE_PAD0)) & 1u))
                cmv_pad_down(d - RFM1_TK_NOTE_PAD0, v);
            else if (!on && ((cmv.pad_held >> (d - RFM1_TK_NOTE_PAD0)) & 1u))
                cmv_pad_up(d - RFM1_TK_NOTE_PAD0);
        } else if (d >= RFM1_TK_NOTE_STEP0 && d < RFM1_TK_NOTE_STEP0 + 16u) {
            const uint32_t i = d - RFM1_TK_NOTE_STEP0;
            if (on) {
                if (cmv.loop) {                    /* Loop held + a step: a loop slot (1..10) */
                    cmv.loop_used = 1;
                    if (i < CRL_SLOTS) {
                        char t[8];
                        if (i != cs.loop_slot)
                            cu_loop_load(i, 0);
                        cmv_mirror_msg();
                        cmv_int(t, (int32_t)i + 1, 0, sizeof t);
                        if ((int32_t)(cmv.msg_until - cmv_now()) <= 0)
                            cmv_msg("Slot", t);
                    }
                } else if (cmv.shift) {            /* Shift + a step (proposal 5): S2 the Settings, S8 the sound
                                                    * editor (their icons lit while Shift is held) */
                    cmv.shift_used = 1;
                    cmv_shift_step(i);
                } else
                    cmv_step_press(i);
            } else if (cmv.step >= 0 && cmv.step_btn == i)
                cmv_step_release((uint32_t)cmv.step);
        } else if (d < 8u) {
            cmv_knob_touch(d, on);
        } else if (d == RFM1_TK_NOTE_JOG_TOUCH) {
            const int home = !cmv_opts() && !cmv_layer() && !cmv_editing() && !cmv.set_open;   /* (a list: the wheel
                                                    * its rows, no sound popup) */
            cmv.wheel_touch = (uint8_t)(on && home);
            if (on && home)
                cmv.shift ? cmv_wheel_bass() : cmv_wheel_sound();
        }
    } else if (st == 0xA0u) {                      /* a pad's pressure: the keys' aftertouch, as the keybed's */
        if (d >= RFM1_TK_NOTE_PAD0 && d < RFM1_TK_NOTE_PAD0 + 32u &&
            (cmv.pad_kind[d - RFM1_TK_NOTE_PAD0] == PK_ROOT || cmv.pad_kind[d - RFM1_TK_NOTE_PAD0] == PK_PTRIG) &&
            (v > cmv.at_last + 1u || v + 1u < cmv.at_last)) {
            cmv.at_last = (uint8_t)v;
            cr_post(CRE_AT, 0, 0, (int32_t)v);
        }
    } else if (st == 0xB0u) {
        if (d >= RFM1_TK_CC_KNOB1 && d < RFM1_TK_CC_KNOB1 + 8u)
            cmv_knob(d - RFM1_TK_CC_KNOB1, RFM1_TK_REL(v));
        else if (d == RFM1_TK_CC_JOG)
            cmv_wheel(RFM1_TK_REL(v));
        else
            cmv_button(d, v != 0);
    }
    if (cmv.shown) {
        cmv_draw();
        cmv_leds();
    }
    fm1_irq_on();
}

/* ------------------------------------------------------------- the screen --- */
static void cmv_scr_strip(int play)                /* the page's 16 steps at the foot: filled = a chord; the playhead tall */
{
    uint32_t i, ns = cmv_nsteps();
    for (i = 0; i < 16u; i++) {
        int x = (int)i * 8, s = (int)(cmv.page * 16u + i);
        if (s == play)
            px_rect(x, 50, 7, 14, 1);
        else if (cmv_step_has(i))
            px_rect(x, 55, 7, 7, 1);
        else
            px_box(x, 55, 7, 7, 1, (uint32_t)s >= ns);
    }
}
static void cmv_note_nm(uint8_t note, char nb[5])  /* a note's name without its octave */
{
    cr_note_name(note, nb);
    nb[(nb[1] == '#' || nb[1] == 'b') ? 2 : 1] = 0;
}
static void cmv_notes_str(char *d, uint32_t n, int maxw)   /* the chord's notes, double spaced when that fits */
{
    const cr_chord_info_t *ci = &cr_snap.ci;
    uint32_t i, k = ci->nnotes > 8u ? 8u : ci->nnotes;
    int sp;
    for (sp = 2; sp >= 1; sp--) {
        d[0] = 0;
        for (i = 0; i < k; i++) {
            char nb[5];
            cmv_note_nm(ci->notes[i], nb);
            if (i)
                cmv_cat(d, sp == 2 ? "  " : " ", n);
            cmv_cat(d, nb, n);
        }
        if (px_tw(d, 2) <= maxw)
            return;
    }
}
/* the chord sounding big at (x, y), cap height h, its notes under it at y 50; none: a dash */
static void cmv_scr_chord(int x, int y, int h, int notes)
{
    const cr_chord_info_t *ci = &cr_snap.ci;
    char nt[40];
    if (!ci->valid) {
        px_rect(52, 28, 24, 5, 1);
        return;
    }
    bt_chord(x, y, ci->root, ci->qual, ci->sup, h, 127 - x);
    if (notes) {
        cmv_notes_str(nt, sizeof nt, 127 - x);
        px_text(x, 50, nt, px_tw(nt, 2) <= 127 - x ? 2 : 1, 1);
    }
}
static void cmv_scr_home(void)                     /* proposal 3: the chord and its notes, little else */
{
    const int ph = cmv_playhead();
    if (ph >= 0) {                                 /* the loop playing: slot top left, step top right, the steps */
        char t[8];
        cmv_int(t, cs.loop_slot + 1, 0, sizeof t);
        px_lbl(0, 0, t);
        cmv_int(t, ph + 1, 0, sizeof t);
        if (!cu.clock)
            px_rlbl(0, t);
        if (cr_snap.ci.valid)
            bt_chord(30, 4, cr_snap.ci.root, cr_snap.ci.qual, cr_snap.ci.sup, 32, 97);
        else
            px_rect(52, 24, 24, 5, 1);
        cmv_scr_strip((uint32_t)ph / 16u == cmv.page ? ph : -1);
    } else if (cmv.mem_down && cmv.lc_mem >= 0) { /* a memory played: its number top left */
        char t[8] = "M";
        cmv_int(t + 1, cmv.lc_mem + 1, 0, sizeof t - 1u);
        px_lbl(0, 0, t);
        cmv_scr_chord(30, 15, 32, 1);
    } else if (cmv_iso()) {
        px_lbl(0, 0, cmv.layout == CMV_ISOK ? "Iso Key" : "Iso");
        cmv_scr_chord(30, 15, 32, 1);
    } else if (cs.key_on) {                        /* Key Mode: the key top left */
        char t[12] = "Key ";
        cmv_cat(t, CU_NOTE[cs.tonic % 12u], sizeof t);
        px_lbl(0, 0, t);
        cmv_scr_chord(30, 15, 32, 1);
    } else
        cmv_scr_chord(4, 6, 34, 1);
    if (cu.clock)                                  /* LOCK on: a lit LK tag */
        px_tag(98, 30, "LK");
}
static void cmv_scr_arp(void)                      /* an arpeggio running: the notes, the one sounding on a block */
{
    char rt[8];
    const cr_chord_info_t *ci = &cr_snap.ci;
    uint32_t i, n = ci->nnotes > 8u ? 8u : ci->nnotes;
    int step = n > 4u ? 128 / (int)n : 32, bw = n > 4u ? step - 2 : 22;
    cmv_cpy(rt, CU_DIV[cs.par[CR_PM_ARP][CR_P_DIV] % CR_DIV_COUNT], sizeof rt);
    px_rlbl(0, rt);
    bt_chord(2, 2, ci->root, ci->qual, ci->sup, 20, 124 - px_tw(rt, 2));
    for (i = 0; i < n; i++) {
        char nb[5];
        int x = 4 + (int)i * step;                 /* (four notes: 4 36 68 100, as the design) */
        cmv_note_nm(ci->notes[i], nb);
        if ((int)i == cr_snap.perf_pos) {
            px_rect(x - 4, 34, bw, 22, 1);
            px_text(x + 2, 38, nb, 2, 0);
        } else
            px_text(x + 2, 44, nb, 2, 1);
    }
    px_rect(0, 61, 128, 1, 1);
}
static void cmv_scr_prog(void)                     /* the Prog layout: the progression's 16 chords of the page */
{
    char nm[CRPG_NAME + 1], right[12], nb[8];
    uint32_t i, maxc;
    int last = cmv.prog_last;
    crp_name(cs.prog_sel, nm);
    if (!nm[0]) {
        cmv_cpy(nm, "PROG ", sizeof nm);
        cmv_int(nm + 5, cs.prog_sel + 1, 0, 4);
    }
    maxc = 10u;                                    /* (the name top left, 2x: ten characters) */
    if (maxc < sizeof nm && nm[maxc])
        nm[maxc] = 0;
    px_lbl(0, 0, nm);
    right[0] = 0;
    if (last >= 0) {                               /* chord k of n (the progression's chords in order) */
        uint8_t nt[CRPG_NOTES];
        uint32_t k = 0, n = 0;
        for (i = 0; i < 48u; i++)
            if (crp_chord(cs.prog_sel, i, nt)) {
                n++;
                if ((int)i <= last)
                    k = n;
            }
        cmv_int(right, (int32_t)k, 0, sizeof right);
        cmv_cat(right, "/", sizeof right);
        cmv_int(nb, (int32_t)n, 0, sizeof nb);
        cmv_cat(right, nb, sizeof right);
    }
    px_rlbl(50, right);
    if (last >= 0) {
        uint8_t nt[CRPG_NOTES];
        uint32_t k = crp_chord(cs.prog_sel, (uint32_t)last, nt), j;
        char r[4], q[5], sup[CR_SUP_MAX];
        int32_t tr = cs.transpose + cs.prog_tr;
        for (j = 0; j < k; j++) {
            int32_t x = (int32_t)nt[j] + tr;
            while (x < 0)
                x += 12;
            while (x > 127)
                x -= 12;
            nt[j] = (uint8_t)x;
        }
        if (k) {
            cr_chord_recognise(nt, (int)k, r, q, sup);
            bt_chord(4, 18, r, q, sup, 30, 122);
            return;
        }
    }
    px_rect(52, 30, 24, 5, 1);
}
static void cmv_scr_slots(void)                    /* Loop held: the ten slots (filled: a loop), the slot big, the tempo */
{
    char t[16];
    uint32_t i;
    for (i = 0; i < CRL_SLOTS; i++) {
        int x = (int)i * 13;
        int full = (int)(((cs.loop_used >> i) & 1u) || (i == cs.loop_slot && cr_snap.llen));
        if (i == cs.loop_slot)
            px_rect(x, 0, 11, 11, 1);
        else if (full) {
            px_box(x, 0, 11, 11, 1, 0);
            px_rect(x + 3, 3, 5, 5, 1);
        } else
            px_box(x, 0, 11, 11, 1, 1);
    }
    cmv_cpy(t, "Loop ", sizeof t);
    cmv_int(t + 5, cs.loop_slot + 1, 0, sizeof t - 5u);
    bt_c(18, t, 22);
    cmv_int(t, (int32_t)cs.bpm, 0, sizeof t);
    cmv_cat(t, " bpm", sizeof t);
    px_clbl(50, t);
}
static void cmv_scr_layout(void)                   /* Menu held: the layout big, a dash each, the bank top right */
{
    static const char *const NM[CMV_NLAYOUT] = {"Keys", "Scale", "Chrom", "Prog", "Iso", "Iso Key"};
    char bk[8] = "B";
    cmv_int(bk + 1, cmv.bank + 1, 0, sizeof bk - 1u);
    px_rlbl(0, bk);
    bt_c(16, NM[cmv.layout % CMV_NLAYOUT], 30);
    px_dashes(CMV_NLAYOUT, cmv.layout, 58, 18, 6);
}
static void cmv_scr_step(void)                     /* a step held: its chord; ratchet, length, chance when not the default */
{
    char t[12], ln[16];
    cmv_cpy(t, "Step ", sizeof t);
    cmv_int(t + 5, cmv.step + 1, 0, 6);
    px_lbl(0, 0, t);
    if (cu_le.cnt) {
        char r[4], q[5], sup[CR_SUP_MAX];
        int32_t pct = CRL_CHANCE_PCT(cu_le.chance < 0 ? 0 : cu_le.chance);
        cu_ev_name(cu_le.root, cu_le.qx, r, q, sup);
        if (cu_le.rat > 0)
            px_rlbl(0, CU_LE_RAT[cu_le.rat % (int)CRL_NRATCH]);
        bt_chord(4, 18, r, q, sup, 28, 122);
        if (cu_le.nf || cu_le.n != 1) {
            if (cu_le.nf)
                cmv_cpy(ln, CU_LE_FRAC[cu_le.nf & 3u], sizeof ln);
            else {
                cmv_int(ln, cu_le.n, 0, sizeof ln);
                cmv_cat(ln, " st", sizeof ln);
            }
            px_lbl(0, 50, ln);
        }
        if (pct != 100) {
            cmv_int(ln, pct, 0, sizeof ln);
            cmv_cat(ln, "%", sizeof ln);
            px_rlbl(50, ln);
        }
    } else {
        px_rect(52, 28, 24, 5, 1);
        px_clbl(50, "play chord");
    }
}
static void cmv_scr_shortcuts(void)                /* Shift held: the FM-1's buttons on the steps, inverted = on */
{
    uint32_t i;
    for (i = 0; i < 8u; i++) {
        int x = (int)(i % 2u) * 66, y = (int)(i / 2u) * 16;
        if (cmv_sc_on(i)) {
            px_rect(x, y, 62, 15, 1);
            px_text(x + 1, y + 1, CMV_SC[i], 2, 0);
        } else
            px_text(x + 1, y + 1, CMV_SC[i], 2, 1);
    }
}
static void cmv_scr_layer(void)                    /* a settings layer: its name, on / off, the page big, a dash each */
{
    char pg[24];
    uint32_t l = cmv_layer(), at, of;
    cmv_layer_page(pg, sizeof pg, &at, &of);
    px_lbl(0, 0, cmv_layer_name(l));
    if (cmv_layer_on(l))
        px_tag(98, 30, "on");
    else
        px_rlbl(0, "off");
    bt_c(18, pg, 30);
    px_dashes(of, at, 58, of <= 4u ? 18 : 10, of <= 4u ? 6 : 4);
}
static void cmv_scr_edit(void)                     /* the editor: the engine, the group big, the screen */
{
    const cr_screen_t *s = cmv_lscreen();
    const uint32_t p = ce.part & 1u;
    char nm[24];
    cmv_cpy(nm, p ? "B." : "", sizeof nm);
    cmv_cat(nm, ENGINES[trk[p ? CR_PART_BASS : CR_PART_CHORD].eng_req % NENGINES]->name, sizeof nm);
    px_lbl(0, 0, nm);
    bt_c(16, CE_SEC_NAME[cx.grp[p] % CE_NSEC], 30);
    px_clbl(50, s->page);
}
static void cmv_scr_settings(void)                /* Shift + S2: RemoteFM1's list (seq_ui.c GLOBAL): four rows, the row
                                                    * inverted, editing: framed, its value inverted between arrows */
{
    uint32_t N, i, top = cmv.set_sel > 1u ? cmv.set_sel - 1u : 0u;
    cmv_list(&N);
    if (top + 4u > N)
        top = N > 4u ? N - 4u : 0u;
    px_lbl(0, 0, cmv.set_open == CMV_L_LOOP ? "Loop" : "Settings");
    px_rect(0, 16, 128, 1, 1);
    for (i = 0; i < 4u && top + i < N; i++) {
        char t[24], v[24];
        const uint32_t r = top + i;
        const int y = 20 + (int)i * 11, sel = r == cmv.set_sel;
        int vx, maxc;
        cmv_set_row(r, t, sizeof t, v, sizeof v);
        vx = 121 - px_tw(v, 1);
        maxc = (vx - 8) / 6;
        if (maxc > 0 && (int)sizeof t > maxc)
            t[maxc] = 0;
        if (sel && cmv.set_edit) {
            px_box(0, y - 2, 124, 11, 1, 0);
            px_text(2, y, t, 1, 1);
            px_rect(vx - 2, y - 1, px_tw(v, 1) + 4, 9, 1);
            px_text(vx, y, v, 1, 0);
            px_text(vx - 8, y, "<", 1, 1);
        } else if (sel) {
            px_rect(0, y - 2, 124, 11, 1);
            px_text(2, y, t, 1, 0);
            px_text(vx, y, v, 1, 0);
        } else {
            px_text(2, y, t, 1, 1);
            px_text(vx, y, v, 1, 1);
        }
    }
    {                                              /* the scroll bar */
        const int h = 44 * 4 / (int)N, y = 19 + (int)(44 - h) * (int)cmv.set_sel / ((int)N - 1);
        px_rect(126, 19, 1, 44, 1);
        px_rect(125, y, 3, h < 3 ? 3 : h, 1);
    }
}
static void cmv_scr_opts(void)                     /* the Options: the group or entry big, the entry's value */
{
    const cr_screen_t *s = cmv_lscreen();
    px_lbl(0, 0, cu.opt_in ? CU_G_NAME[cu.opt_g] : "Options");
    bt_c(18, s->n_items ? s->item[(uint32_t)(s->sel - s->item0) % CR_PICK_MAX] : "-", 22);
    px_clbl(50, s->value);
}
static void cmv_scr_popup(const char *title, const char *value, int32_t pct)   /* full screen: the name, the value big */
{
    px_clbl(0, title);
    bt_c(18, value, 30);
    if (pct >= 0) {
        px_box(10, 58, 108, 5, 1, 0);
        px_rect(11, 59, (int)(106 * (pct > 1000 ? 1000 : pct) / 1000), 3, 1);
    }
}
static void cmv_scr_voicing(const char *v)         /* Voicing turned: the revoiced chord, its notes, the voicing */
{
    const cr_chord_info_t *ci = &cr_snap.ci;
    char t[40];
    bt_chord(4, 2, ci->root, ci->qual, ci->sup, 26, 122);
    cmv_notes_str(t, sizeof t, 123);
    px_lbl(4, 36, t);
    cmv_cpy(t, "voicing ", sizeof t);
    cmv_cat(t, v, sizeof t);
    px_lbl(4, 50, t);
}
static void cmv_scr_panic(void)                    /* PANIC: inverted */
{
    px_rect(0, 0, 128, 64, 1);
    {
        int h = bt_fit("PANIC", 22, 126);
        bt(64 - bt_w("PANIC", h) / 2, 8, "PANIC", h, 0);
    }
    px_ctext(64, 48, "all off", 2, 0);
}
static void cmv_scr_wheel(void)                    /* the wheel's sound: the preset or the engine, the chord's or the bass's */
{
    const uint32_t bass = cmv.wheel_k >= 2, eng = (uint32_t)cmv.wheel_k & 1u;
    const uint32_t e = trk[bass ? CR_PART_BASS : CR_PART_CHORD].eng_req % NENGINES;
    char t[24], nm[13];
    if (bass)
        px_tag(0, 52, "Bass");
    if (eng) {
        uint32_t n = 0;
        int32_t r = cu_engine_rank(e);
        cu_engine_at(0, &n);
        if (!bass)
            px_lbl(0, 0, "Engine");
        if (r >= 0 && n) {
            cmv_int(t, r + 1, 0, sizeof t);
            cmv_cat(t, "/", sizeof t);
            cmv_int(nm, (int32_t)n, 0, sizeof nm);
            cmv_cat(t, nm, sizeof t);
            px_rlbl(0, t);
            px_dashes(n, (uint32_t)r, 58, 12, 4);
        }
        bt_c(20, ENGINES[e]->name, 20);
        return;
    }
    if (bass) {
        if (!cs.bass_sound || !cs.bass_on) {
            bt_c(22, "Off", 20);
            return;
        }
        pool_name(e, cs.bass_sound - 1u, nm);
        cmv_cpy(t, ENGINES[e]->name, sizeof t);
        cmv_cat(t, " ", sizeof t);
        cmv_int(nm, cs.bass_sound, 0, 4);
        cmv_cat(t, nm, sizeof t);
        pool_name(e, cs.bass_sound - 1u, nm);
        if (px_tw(t, 2) > 70)
            cmv_int(t, cs.bass_sound, 0, 4);
        px_rlbl(0, t);
        bt_c(22, nm, 20);
        return;
    }
    pool_name(e, cs.sound, nm);
    px_lbl(0, 0, ENGINES[e]->name);
    cmv_int(t, (int32_t)cs.sound + 1, 0, sizeof t);
    px_rlbl(0, t);
    bt_c(22, nm, 18);
    px_clbl(50, cu_fav_part(0) ? "favourite" : "preset");
}
static void cmv_scr_saved(void)                    /* a memory saved: the chord, a lit block with its number */
{
    char t[8] = "M";
    int h = bt_fit(cmv.saved_name, 32, 80), hb;
    cmv_int(t + 1, cmv.saved_n, 0, sizeof t - 1u);
    bt(2, 12 + (32 - h), cmv.saved_name, h, 1);
    px_rect(84, 8, 44, 48, 1);
    hb = bt_fit(t, 18, 40);
    bt(106 - bt_w(t, hb) / 2, 14, t, hb, 0);
    px_text(94, 40, "ok", 2, 0);
}
static void cmv_draw(void)
{
    const uint32_t now = cmv_now();
    cmv_px = rfm1_tk_screen();
    if (!cmv_px)
        return;
    memset(cmv_px, 0, RFM1_TK_SCREEN_BYTES);
    if ((int32_t)(cmv.saved_until - now) > 0)
        cmv_scr_saved();
    else if ((int32_t)(cmv.msg_until - now) > 0) {
        if (cmv_same(cmv.msg_t, "PANIC"))
            cmv_scr_panic();
        else
            cmv_scr_popup(cmv.msg_t, cmv.msg_v, -1);
    } else if (cmv.pop_kind >= 0 && (cmv.pop_touch || (int32_t)(cmv.pop_until - now) > 0)) {
        char t[20], v[24];
        int32_t pct;
        if (cmv_kv(cmv.pop_kind, t, sizeof t, v, sizeof v, &pct)) {
            if (cmv.pop_kind == K_VOIC && cr_snap.ci.valid && !cmv_layer() && !cmv_editing() && !cmv_opts())
                cmv_scr_voicing(v);
            else
                cmv_scr_popup(t, v, pct);
        }
    } else if (cmv.set_open)                       /* (a list: straight in, over Shift and the wheel's popups) */
        cmv_scr_settings();
    else if (cmv.wheel_touch || (int32_t)(cmv.wheel_until - now) > 0) {
        if (cmv.wheel_k >= 0)
            cmv_scr_wheel();
        else
            cmv_scr_popup(cmv.wheel_t, cmv.wheel_v, -1);
    } else if (cmv.step >= 0)
        cmv_scr_step();
    else if (cmv.menu && (cmv.menu_used || now - cmv.menu_t0 >= CMV_HOLD_MS))
        cmv_scr_layout();
    else if (cmv.loop)
        cmv_scr_slots();
    else if (cmv.shift)
        cmv_scr_shortcuts();
    else if (cmv_opts())
        cmv_scr_opts();
    else if (cmv_layer())
        cmv_scr_layer();
    else if (cmv_editing())
        cmv_scr_edit();
    else if (cmv.layout == CMV_PROG)
        cmv_scr_prog();
    else if (cr_snap.perform_on && cr_snap.perform_mode == CR_PM_ARP && cr_snap.perf_step && cr_snap.ci.valid &&
             !cr_snap.perf_part)
        cmv_scr_arp();
    else
        cmv_scr_home();
    if (cmv.pop_kind >= 0 && !cmv.pop_touch && (int32_t)(cmv.pop_until - now) <= 0)
        cmv.pop_kind = -1;
    rfm1_tk_screen_done();
}

/* -------------------------------------------------------------- the LEDs --- */
static const uint8_t CMV_SC_COL[SC_N][3] = {{C_YELLOW}, {C_VIOLET}, {C_WHITE}, {C_VIOLET}, {C_BASS}, {C_GREEN},
                                           {C_BASS}, {C_RED}};
static const uint8_t CMV_GRP_COL[CE_NSEC][3] = {{C_CYAN}, {C_ORANGE}, {C_YELLOW}, {C_GREEN}, {C_VIOLET}, {C_RED},
                                               {C_WHITE}};
static void led(int i, uint8_t r, uint8_t g, uint8_t b) { rfm1_tk_led(i, r, g, b); }
static void led_dim(int i, uint8_t r, uint8_t g, uint8_t b, uint32_t pct)
{
    rfm1_tk_led(i, (uint8_t)(r * pct / 100u), (uint8_t)(g * pct / 100u), (uint8_t)(b * pct / 100u));
}
static void cmv_led_pads(void)
{
    const uint32_t now = cmv_now();
    int r, c;
    uint8_t deg[12];
    uint32_t ndeg = cmv_degrees(deg);
    uint16_t sc = cr_scale_bits(cs.tonic, cs.scale);   /* (proposal 6: the KEY settings' key, Key Mode on or off) */
    uint32_t ton = cs.tonic % 12u;
    const int picker = cmv.menu && (cmv.menu_used || now - cmv.menu_t0 >= CMV_HOLD_MS);
    uint32_t hl_mods = 0;                          /* a step held: its chord's root and keys lit white (proposal 5) */
    int hl_root = -1;
    if (cmv.step >= 0 && cu_le.ok && cu_le.cnt) {
        const uint32_t q = (uint32_t)cu_le.qx >> 4, x = (uint32_t)cu_le.qx & 15u;
        if (q != CR_Q_PROG) {
            hl_root = (((int)cu_le.root - cs.transpose) % 12 + 12) % 12;
            hl_mods = (q >= 1u && q <= 4u ? 1u << (q - 1u) : 0u) | x << 4;
        }
    }
    for (r = 0; r < 4; r++)
        for (c = 0; c < 8; c++) {
            const int n = cmv_pad(r, c), held = (int)((cmv.pad_held >> n) & 1u);
            if (picker) {                          /* layout E */
                if (r == 3 && c < 4)
                    c == cmv.layout ? led(n, C_WHITE) : led_dim(n, C_WHITE, c == cmv.prev ? 45u : 15u);
                else if (r == 2 && c < 2) {        /* ISO, ISO in key */
                    const uint32_t L = c ? CMV_ISOK : CMV_ISO;
                    cmv.layout == L ? led(n, C_WHITE) : led_dim(n, C_WHITE, cmv.prev == L ? 45u : 15u);
                }
                else if (r == 2 && c < 4)
                    led(n, 0, 0, 0);
                else if (r >= 2) {
                    uint32_t b = (uint32_t)((3 - r) * 4 + (c - 4)), j, any = 0;
                    for (j = 0; j < 8u; j++)
                        any |= cmv.mem[b * 8u + j].used;
                    b == cmv.bank ? led(n, C_MEM) : led_dim(n, C_MEM, any ? 50u : 15u);
                } else {
                    uint32_t p = r == 1 ? (uint32_t)c : 8u + (uint32_t)c;
                    p == cs.prog_sel && cmv.layout == CMV_PROG ? led(n, C_WHITE) :
                        led_dim(n, C_PROG, crp_count(p) ? 85u : 12u);
                }
                continue;
            }
            if (cmv.shift && cmv_sc_pad(r, c) >= 0) {   /* Shift: the FM-1's buttons, full = on (proposal 5) */
                const uint8_t *k = CMV_SC_COL[cmv_sc_pad(r, c)];
                held ? led(n, C_WHITE) : cmv_sc_on((uint32_t)cmv_sc_pad(r, c)) ? led(n, k[0], k[1], k[2]) :
                    led_dim(n, k[0], k[1], k[2], 18u);
                continue;
            }
            if (cmv.shift && !(r >= 2 && c >= 4)) {    /* Shift: the rest dark (proposal 6) */
                led(n, 0, 0, 0);
                continue;
            }
            if (cmv_editing() && r >= 2 && c >= 4) {   /* the editor's groups, each its colour; the eighth: chord / bass */
                uint32_t j = (uint32_t)((3 - r) * 4 + (c - 4));
                if (held)
                    led(n, C_WHITE);
                else if (j >= CE_NSEC)
                    ce.part ? led(n, C_BASS) : led_dim(n, C_BASS, 25u);
                else {
                    const uint8_t *k = CMV_GRP_COL[j];
                    j == cx.grp[ce.part & 1u] ? led(n, k[0], k[1], k[2]) :
                        led_dim(n, k[0], k[1], k[2], ce_has(ce_trk(), j) ? 22u : 6u);
                }
                continue;
            }
            if (r >= 2 && c < 4 && !cmv_iso()) {   /* the chord keys */
                uint32_t j = (uint32_t)((3 - r) * 4 + c);
                if (held || ((hl_mods >> j) & 1u))
                    led(n, C_WHITE);
                else if ((cu.mlatch >> j) & 1u)
                    j < 4u ? led(n, C_VIOLET) : led(n, C_ORANGE);
                else
                    j < 4u ? led(n, C_VIOLET_D) : led(n, C_ORANGE_D);
                continue;
            }
            if (r >= 2 && (!cmv_iso() || cmv.shift)) {   /* the memories */
                uint32_t j = (uint32_t)((3 - r) * 4 + (c - 4));
                if ((cmv.flash_pad == n && (int32_t)(cmv.flash_until - now) > 0) || held)
                    led(n, C_WHITE);
                else
                    cmv.mem[cmv.bank * 8u + j].used ? led(n, C_MEM) : led_dim(n, C_MEM, 18u);
                continue;
            }
            if (cmv.layout == CMV_PROG) {
                uint32_t i = cmv.prog_page * 16u + (uint32_t)(r * 8 + c);
                uint8_t nt[CRPG_NOTES];
                if (held)
                    led(n, C_WHITE);
                else if (i < CRPG_KEYS && crp_chord(cs.prog_sel, i, nt))
                    led_dim(n, C_PROG, 85u);
                else
                    led(n, 14, 14, 18);
                continue;
            }
            {
                int note = cmv_pad_note(r, c);
                if (note < 0)
                    led(n, 0, 0, 0);
                else if (held || (hl_root >= 0 && (note % 12) == hl_root))
                    led(n, C_WHITE);
                else if (cmv.layout == CMV_KEYS)   /* the key's tonic yellow, its notes grey, the rest dim (proposal 6) */
                    (uint32_t)note % 12u == ton ? led(n, C_YELLOW) :
                        (sc >> (note % 12) & 1u) ? led(n, C_KEY_D) : led(n, 16, 16, 18);
                else if (cmv.layout == CMV_SCALE)
                    (c == 0 || (uint32_t)c == ndeg) ? led(n, C_YELLOW) : led(n, C_KEY_D);
                else if ((uint32_t)note % 12u == ton)
                    led(n, C_YELLOW);
                else
                    (sc >> (note % 12) & 1u) ? led_dim(n, C_WHITE, 32u) : led(n, 12, 12, 14);
            }
        }
}
static void cmv_led_steps(void)
{
    uint32_t i, ns;
    int ph = cmv_playhead();
    if (cmv.loop) {                                /* Loop held: the slots */
        for (i = 0; i < 16u; i++) {
            const int L = RFM1_TK_L_STEP0 + (int)i;
            if (i < CRL_SLOTS) {
                int full = (int)(((cs.loop_used >> i) & 1u) || (i == cs.loop_slot && cr_snap.llen));
                i == cs.loop_slot ? led(L, C_HELD) : led_dim(L, C_SEQ, full ? 60u : 12u);
            } else
                led(L, 0, 0, 0);
        }
    } else if (cmv.shift) {                        /* Shift held (proposal 5): the steps with a job, their icons lit:
                                                    * S2 the Settings (white), S8 the sound editor (cyan) */
        static const uint8_t SC[CMV_S_N][3] = {{C_WHITE}, {C_WHITE}, {C_SEQ}, {C_SEQ}, {C_SEQ}, {C_SEQ}, {C_SEQ},
                                               {C_CYAN}, {C_YELLOW}, {C_RED}, {C_RED}, {C_RED}};
        for (i = 0; i < 16u; i++) {
            const int L = RFM1_TK_L_STEP0 + (int)i;
            if (i < CMV_S_N)
                cmv_sstep_on(i) ? led(L, SC[i][0], SC[i][1], SC[i][2]) : led_dim(L, SC[i][0], SC[i][1], SC[i][2], 22u);
            else
                led(L, 0, 0, 0);
        }
        for (i = 0; i < 16u; i++)                  /* (the icons: the steps with a Shift job) */
            rfm1_tk_white(RFM1_TK_L_ICON0 + (int)i, i < CMV_S_N ? 255 : 0);
        return;
    } else {
        ns = cmv_nsteps();
        for (i = 0; i < 16u; i++) {
            const int L = RFM1_TK_L_STEP0 + (int)i, s = (int)(cmv.page * 16u + i);
            if (s < 64 && ((cmv.marks >> s) & 1u) && cu_blink(cmv_now()))   /* the clipboard's: white copied, red cut */
                cmv.mark_cut ? led(L, C_RED) : led(L, C_WHITE);
            else if (s == cmv.step)
                led(L, C_HELD);
            else if (s == ph)
                led(L, C_WHITE);
            else if ((uint32_t)s >= ns)
                led(L, 0, 0, 0);
            else if (cmv_step_has(i))
                led(L, C_SEQ);
            else
                led(L, 24, 24, 30);
        }
    }
    ns = (cmv_nsteps() + 15u) / 16u;               /* the icons under the steps: the pages, only for a loop of more than one */
    for (i = 0; i < 16u; i++)
        rfm1_tk_white(RFM1_TK_L_ICON0 + (int)i, ns > 1u && i < ns ? (i == cmv.page ? 255 : 64) : 0);
}
static void cmv_leds(void)
{
    static const uint8_t RING[3][8][3] = {
        {{C_ORANGE}, {C_BASS}, {C_VIOLET}, {C_GREEN}, {C_YELLOW}, {C_YELLOW}, {C_YELLOW}, {C_WHITE}},
        {{C_SEQ}, {C_SEQ}, {C_RED}, {C_RED}, {C_SEQ}, {C_RED}, {C_RED}, {C_WHITE}},
        {{C_HELD}, {C_HELD}, {C_HELD}, {C_HELD}, {C_HELD}, {C_HELD}, {C_HELD}, {C_SEQ}}};
    const uint32_t now = cmv_now();
    const int pg = cmv.step >= 0 ? 2 : cmv.loop ? 1 : 0;
    const int blink = cu_blink(now);
    uint32_t k;
    cmv_led_pads();
    cmv_led_steps();
    if (cmv.step < 0 && !cmv.loop && (cmv.set_open || cmv_opts() || cmv_layer() || cmv_editing())) {   /* the knobs with a job lit,
                                                    * in the layer's colour */
        static const uint8_t LC[L_N][3] = {[L_KEY] = {C_YELLOW}, [L_LOCK] = {C_VIOLET}, [L_PERF] = {C_VIOLET},
                                           [L_FX] = {C_GREEN}, [L_BASS] = {C_BASS}, [L_METRO] = {C_RED}};
        const uint32_t l = cmv_layer();
        const uint8_t *c = cmv_opts() ? (const uint8_t *)"\xff\xff\xff" : l ? LC[l] :
                           (const uint8_t *)"\x13\xab\xbf";
        for (k = 0; k < 8u; k++) {
            char t[20], v[24];
            int32_t pct;
            cmv_kv((int)(K_LAY0 + k), t, sizeof t, v, sizeof v, &pct) ?
                (l == L_PERF && cu.perf_part ? led(RFM1_TK_L_KNOB1 + (int)k, C_BASS) :
                 led(RFM1_TK_L_KNOB1 + (int)k, c[0], c[1], c[2])) : led(RFM1_TK_L_KNOB1 + (int)k, 0, 0, 0);
        }
    } else
        for (k = 0; k < 8u; k++)
            led(RFM1_TK_L_KNOB1 + (int)k, RING[pg][k][0], RING[pg][k][1], RING[pg][k][2]);
    cmv_playing() ? led(RFM1_TK_L_PLAY, 0, 200, 0) : led(RFM1_TK_L_PLAY, 0, 40, 0);
    if (cr_snap.lcap == CRL_CAP_REC || cr_snap.lcap == CRL_CAP_OD || cr_snap.lcap == CRL_CAP_COUNTIN)
        led(RFM1_TK_L_REC, blink ? 255 : 60, 0, 0);
    else if (cr_snap.lcap != CRL_CAP_NONE)
        led(RFM1_TK_L_REC, 255, 0, 0);
    else
        led(RFM1_TK_L_REC, 0x44, 0, 0);
    cu_fav_part(0) ? led(RFM1_TK_L_SAMPLE, C_WHITE) : led(RFM1_TK_L_SAMPLE, 40, 40, 40);
    rfm1_tk_white(RFM1_TK_L_SHIFT, cmv.shift ? 255 : 60);
    rfm1_tk_white(RFM1_TK_L_MENU, cmv.menu ? 255 : 77);
    rfm1_tk_white(RFM1_TK_L_BACK, cmv_opts() || cmv_layer() || cmv_editing() ? 255 : 77);
    rfm1_tk_white(RFM1_TK_L_CAPTURE, 77);
    rfm1_tk_white(RFM1_TK_L_DOWN, cu.octave < 0 ? 255 : 77);
    rfm1_tk_white(RFM1_TK_L_UP, cu.octave > 0 ? 255 : 77);
    rfm1_tk_white(RFM1_TK_L_UNDO, 77);
    rfm1_tk_white(RFM1_TK_L_LOOP, cs.sticky || cmv.loop ? 255 : 77);
    rfm1_tk_white(RFM1_TK_L_COPY, cmv.copy ? 255 : cmv.paste_armed ? (blink ? 255 : 40) : cmv.marks ? 180 : 77);
    rfm1_tk_white(RFM1_TK_L_LEFT, cmv.page ? 77 : 20);
    rfm1_tk_white(RFM1_TK_L_RIGHT, cmv.page < 3u ? 77 : 20);
    rfm1_tk_white(RFM1_TK_L_MUTE, cs.bass_on ? 255 : 40);
    rfm1_tk_white(RFM1_TK_L_DELETE, cmv.del ? 255 : 77);
}

/* ------------------------------------------------------------ the frame --- */
static void cmv_speak_chord(void)                  /* the player's chord as it changes: "Chord D minor seven" */
{
    char nm[24], s[64], w[48];
    const cr_chord_info_t *ci = &cr_snap.ci;
    if (!cmv.speech || !ci->valid || !ci->sounding || cr_snap.ldisp)
        return;
    cmv_chord_str(nm, sizeof nm, ci->root, ci->qual, ci->sup);
    if (cu_eq(nm, cmv.said_chord))
        return;
    cmv_cpy(cmv.said_chord, nm, sizeof cmv.said_chord);
    if (cmv.step >= 0 || (int32_t)(cmv.saved_until - cmv_now()) > 0)
        return;                                    /* (the step or the memory says its own) */
    cmv_cpy(s, "Chord ", sizeof s);
    cmv_words(nm, w, sizeof w);
    cmv_cat(s, w, sizeof s);
    cmv_say(s);
}
/* the empty bar: on when a queued switch to an empty slot is taken while the loop plays (crl.loads moves, the loop
 * EMPTY); off with Play, a take, leaving the takeover. A loop arriving in the slot meanwhile (a step written, another
 * slot picked) starts at once in phase with the bar, as if it had been playing all along */
static void cmv_ghost_frame(void)
{
    const int play = crl.state == CRL_PLAYING;
    if (!cmv.ghost && cmv.shown && cmv.was_play && crl.loads != cmv.loads_seen && crl.state == CRL_EMPTY) {
        cmv.ghost = 1;
        cmv.ghost_rt0 = crl.rcyc0;                 /* (the cycle's end: the bar's start) */
        cu_trace("move: empty slot %u, the bar runs on\n", (unsigned)cs.loop_slot + 1u);
    }
    cmv.loads_seen = crl.loads;
    cmv.was_play = (uint8_t)play;
    if (!cmv.ghost)
        return;
    if (!cmv.shown || crl.cap != CRL_CAP_NONE || play) {
        cmv.ghost = 0;
        return;
    }
    if (crl.state == CRL_STOPPED && crl.d.len && !cu.ledit) {   /* a loop here now: in phase (its keys let go) */
        uint32_t ph = (crl.rt - cmv.ghost_rt0) % crl.d.len;
        crl.rate = crl.rate_want;
        crl.rfrac = 0;
        crl.cyc0 = crl.t - ph;
        crl.rcyc0 = crl.rt - ph;
        crl.scan = ph;
        crl.pass12 = crl.pass_n = 0;
        crl.hit_on = 0;
        crl.state = CRL_PLAYING;
        cr_snap.lstate = CRL_PLAYING;
        cmv.ghost = 0;
        cmv.was_play = 1;
        cu_trace("move: slot %u joins the bar at tick %u\n", (unsigned)cs.loop_slot + 1u, (unsigned)ph);
    }
}
static void cmv_frame(void)                        /* emu_fw.c emu_fw_frame, after cr_ui_frame (the snapshot is new) */
{
    if (!cmv.inited)
        return;
    fm1_irq_off();
    if (cmv.pads_dirty)
        cmv_pads_sync();
    else {
        static uint32_t chk;
        if (!(++chk & 63u))                        /* (the engine's pads as the bank says: a panic, a revert) */
            cmv_pads_sync();
    }
    cmv_ghost_frame();
    if (cmv.keys_down || cmv.mem_down) {           /* a key or a memory held: what it sounds is the player's chord */
        const cr_chord_t lc = cr.last_chord;
        if (lc.used && (lc.root != cmv.mine.root || lc.q != cmv.mine.q || lc.x != cmv.mine.x || !cmv.mine.used))
            cmv.mine = lc;
    }
    if (cmv.sc && !cmv.sc_open && cmv_now() - cmv.sc_t0 >= CMV_HOLD_MS) {   /* Shift + a step held: its settings */
        cmv.sc_open = 1;
        cmv_sc_hold(cmv.sc - 1u);
    }
    if (cmv.step >= 0 && !cu.ledit)                /* (the editor closed under the hold: the FM-1, a capture) */
        cmv.step = -1, cmv.step_ed = 0;
    if (cmv.step >= 0 && !cmv.step_prev && !cmv.step_used && !cmv.copy && !cmv.del && !cmv_playing() &&
        cmv_now() - cmv.step_t0 >= CMV_HOLD_MS) {  /* a step held, stopped: its chord heard once (a tap: not) */
        cmv.step_prev = 1;
        cu.le_cell = (uint16_t)cmv.step;
        cu_le.ok = 0;
        cu_le_sync();
        if (cu_le.cnt)
            cr_loop_ed_preview(&crl, &cr);
    }
    if (cmv.step >= 0) {
        cu.le_cell = (uint16_t)cmv.step;
        cu_le_sync();
        if (cmv.step_gen != crl.gen && !cmv.pad_held) {   /* written (its keys up): "Step 9, C major seven" */
            cmv.step_gen = crl.gen;
            if (cmv.shown && cmv.pop_kind < 0)
                cmv_step_say();
        }
    }
    if (cmv.shown) {
        cmv_speak_chord();
        cmv_draw();
        cmv_leds();
    }
    fm1_irq_on();
}
static void cmv_state(int on, unsigned claims, unsigned flags)
{
    (void)claims;
    fm1_irq_off();
    cmv.shown = (uint8_t)on;
    cr_tk_clock = (uint8_t)on;                     /* (proposal 6: shown, the clock and the loop's start / stop go out:
                                                    * RemoteFM1's sequencer follows, so Play is the Move's) */
    cmv.speech = (uint8_t)((flags & RFM1_TK_F_SPEECH) != 0u);
    if (!on) {                                     /* gone: (the host released what it held) nothing left held */
        uint32_t n;
        for (n = 0; n < 32u; n++)
            if ((cmv.pad_held >> n) & 1u)
                cmv_pad_up(n);
        if (cmv.step >= 0) {
            cmv.step_used = 1;
            cmv_step_release((uint32_t)cmv.step);
        }
        cmv.shift = cmv.menu = cmv.del = cmv.loop = cmv.sc = cmv.copy = 0;
        cmv.pop_touch = 0;
        cmv.wheel_touch = 0;
    } else {
        cmv.said_chord[0] = 0;
        cmv.map_ok = 0;
        cmv_draw();
        cmv_leds();
    }
    cu_trace("move: takeover %s%s\n", on ? "on" : "off", cmv.speech ? " (speech)" : "");
    fm1_irq_on();
}
static const rfm1_tk_ops_t CMV_OPS = {cmv_input, cmv_state};
static void cmv_init(void)                         /* emu_fw.c emu_fw_init, after cr_ui_init (the session is read) */
{
    cmv.inited = 1;
    cmv.step = -1;
    if (cmv.layout == CMV_SCALE || cmv.layout >= CMV_NLAYOUT)
        cmv.layout = CMV_KEYS;
    rfm1_tk_register("ChoralRoot", RFM1_TK_ALL & ~(RFM1_TK_TRACKS | RFM1_TK_VOLUME), &CMV_OPS);
    cmv_pads_sync();
    cmv_px = rfm1_tk_screen();
    if (cmv_px) {
        cmv_draw();
        cmv_leds();
    }
}
