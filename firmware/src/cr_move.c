/* SPDX-License-Identifier: GPL-3.0-only */
/* cr_move.c: ChoralRoot's own Ableton Move interface, "CHORDS" (FM1VST docs/RFM1-TAKEOVER.md, capability tk1; the
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
 *   Right: 17..32, 33..48). Menu tapped: the previous layout; held: the picker (layouts, banks, progressions).
 *   Steps: the loop slot's pattern, 16 a page (Left / Right: the page, up to 64 steps); tap: an empty step gets the
 *   last chord played, a filled one is cleared; held: keys / memories write it, the knobs edit it. Shift + step 1..10:
 *   the loop slot; Shift + 13..16: the page.
 *   Knobs: Voicing Perform Rate Direction Bass reg. Key Scale Transpose; Shift: Sound, Bass sound, Gate, Swing, Bass
 *   level, Velocity, Humanize, Tempo; a step held: Note len, Chance, Velocity, Ratchet, Condition, Voicing,
 *   Transpose, Steps. Touched or turned: a full-screen popup, the name, then the value big.
 *   The wheel: the chord sound (Shift: its engine); its click: the sound's favourite mark. Play: the sequencer play /
 *   stop (Shift: restart); Rec: by the record mode (Shift: the next mode); Undo: the looper's undo; Loop: LATCH;
 *   Copy: LOCK; Mute: the bass on / off; Delete: held, the clear modifier; tapped alone, PANIC; Capture: the global
 *   save; Back: closes a popup. Sample: nothing.
 * Deterministic but for the time (fm1_ms): holds, popups and flashes are timed. */
#include "rfm1_move.h"
#include "cr_move_font.h"

#define CMV_HOLD_MS 350u                           /* a tap is shorter (Menu, a step) */
#define CMV_POP_MS 1000u                           /* a knob's popup after its last turn / its release */
#define CMV_MSG_MS 1200u                           /* a message (saved, refused, PANIC) */
#define CMV_FLASH_MS 300u                          /* a memory pad's white flash when it is saved */
#define CMV_NMEM 64u                               /* 8 banks of 8 */
#define CMV_TRIG0 100u                             /* the engine's trigger ids of the Prog layout's pads (cr_prog_key:
                                                    * apart from the keybed's 0..47) */
#define CMV_GID 0xFFFFFF00u                        /* the gesture id of a step written from the last chord */

enum { CMV_KEYS, CMV_SCALE, CMV_CHROM, CMV_PROG, CMV_NLAYOUT };
static const char *const CMV_LAYOUT[CMV_NLAYOUT] = {"KEYS", "SCALE", "CHROM", "PROG"};
static const char *const CMV_LAYOUT_SAY[CMV_NLAYOUT] = {"Keys", "Scale", "Chromatic", "Prog"};

/* the pad kinds a press started (its release ends what the press started, whatever the layout became) */
enum { PK_NONE, PK_ROOT, PK_PTRIG, PK_MEM, PK_MOD, PK_PICK };

typedef struct { uint8_t used, q, x, vel; int16_t root; int8_t voic; } cmv_mem_t;   /* a cr_chord_t + the voicing */

/* the knobs' jobs (a page of eight each: main, Shift, a step held) */
enum { K_VOIC, K_PERF, K_RATE, K_DIR, K_BREG, K_KEY, K_SCALE, K_TRANS,
       K_SOUND, K_BSOUND, K_GATE, K_SWING, K_BLEVEL, K_VEL, K_HUM, K_TEMPO,
       K_S_LEN, K_S_CHANCE, K_S_VEL, K_S_RAT, K_S_COND, K_S_VOIC, K_S_TR, K_S_STEPS, K_NKIND };
static const char *const CMV_KNAME[K_NKIND] = {
    "Voicing", "Perform", "Rate", "Direction", "Bass reg.", "Key", "Scale", "Transpose",
    "Sound", "Bass sound", "Gate", "Swing", "Bass level", "Velocity", "Humanize", "Tempo",
    "Note len", "Chance", "Velocity", "Ratchet", "Condition", "Voicing", "Transpose", "Steps"};
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
} cmv = {.step = -1, .pop_kind = -1, .flash_pad = -1, .prog_last = -1, .lc_mem = -1, .prev = CMV_SCALE};

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
        return cmv_base() + r * 5 + c;
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
    lc = cr.last_chord;
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
#define CMV_TAIL_LEN (8u + 7u * CMV_NMEM)
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
}
static void cmv_tail_get(const uint8_t *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < CMV_NMEM; i++)
        cmv.mem[i].used = 0;
    cmv.bank = 0;
    cmv.layout = CMV_KEYS;
    cmv.prev = CMV_SCALE;
    if (s && n >= CMV_TAIL_LEN && s[0] == (uint8_t)CMV_STORE_MAGIC && s[1] == (uint8_t)(CMV_STORE_MAGIC >> 8) &&
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
    if (cmv_rd32(cmv_sbuf) != CMV_STORE_MAGIC || n != CMV_TAIL_LEN ||
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
    char s[24];
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
static int cmv_playhead(void)                      /* the step playing (of the loop), -1 none */
{
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
    if (s >= n && s < CRL_STEPS) {                 /* past the loop's end: the loop to the end of this page */
        uint32_t to = (s / 16u + 1u) * 16u;
        int ok;
        fm1_irq_off();
        ok = cr_loop_ed_len(&crl, (to > CRL_STEPS ? CRL_STEPS : to) * CRL_STEP);
        fm1_irq_on();
        if (ok)
            cmv.step_ext = (uint8_t)(n + 1u);
        cu_le.ok = 0;
        cu.le_cell = (uint16_t)s;
        cu_le_sync();
    }
    if (cmv.del) {                                 /* Delete held + a step: cleared */
        cmv.del_used = 1;
        cu_ledit_clear();
        cmv.step_used = 1;
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
    if (cmv.pop_kind >= K_S_LEN)
        cmv.pop_kind = -1;
}

/* ------------------------------------------------------------- the knobs --- */
static uint32_t cmv_perf_idx(void) { return cs.perform_on ? cs.perf_sel + 1u : 0u; }
static int cmv_knob_kind(uint32_t k)
{
    if (cmv.step >= 0) {
        static const uint8_t ST[8] = {K_S_LEN, K_S_CHANCE, K_S_VEL, K_S_RAT, K_S_COND, K_S_VOIC, K_S_TR, K_S_STEPS};
        return ST[k];
    }
    return (int)(cmv.shift ? K_SOUND + k : K_VOIC + k);
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
    case K_RATE: cu_param_turn(CU_PF_CHORD, mode, CU_PERF_MAIN[mode], s, 0); break;
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
        if (kind >= K_S_LEN && cmv.step >= 0)
            cmv_step_knob(kind, s);
        break;
    }
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
    case K_SWING:
        p = kind == K_RATE ? CU_PERF_MAIN[mode] : kind == K_DIR ? (mode == CR_PM_PATTERN ? CR_P_DIV : CR_P_DIR) :
            kind == K_GATE ? CR_P_GATE : CR_P_SWING;
        v = cs.par[mode][p];
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
static void cmv_pop_say(int kind)
{
    char v[24], s[64], w[48];
    int32_t pct;
    cmv_value(kind, v, sizeof v, &pct);
    cmv_cpy(s, CMV_KNAME[kind], sizeof s);
    cmv_cat(s, " ", sizeof s);
    cmv_vwords(v, w, sizeof w);
    cmv_cat(s, w, sizeof s);
    cmv_say(s);
}
static void cmv_knob(uint32_t k, int32_t d)
{
    int kind = cmv_knob_kind(k);
    if (!d)
        return;
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
    cmv.pop_touch = (uint8_t)(on ? cmv.pop_touch | (1u << k) : cmv.pop_touch & ~(1u << k));
    if (on) {
        if (kind >= K_S_LEN) {
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
        if (nm[0]) {
            cmv_cat(s, ", ", sizeof s);
            cmv_cat(s, nm, sizeof s);
        }
        cmv_say(s);
        cu_trace("move: progression %u\n", (unsigned)p + 1u);
    }
}
static void cmv_pad_down(uint32_t n, uint32_t vel)
{
    int r = (int)(n / 8u), c = (int)(n % 8u);
    cmv.pad_held |= 1u << n;
    cmv.pad_kind[n] = PK_NONE;
    cu.last_input = cmv_now();
    if (cmv.step >= 0)
        cmv.step_used = 1;
    if (cmv.menu) {                                /* the picker */
        cmv.pad_kind[n] = PK_PICK;
        cmv_picker_pad(r, c);
        return;
    }
    if (r >= 2 && c < 4) {                         /* a chord key */
        uint32_t j = (uint32_t)((3 - r) * 4 + c);
        if (cmv.shift && !cu.clock) {              /* Shift + a chord key: LOCK on, the key latched with it */
            cu_lock_tap();
            cmv.shift_used = 1;
        }
        cmv.pad_kind[n] = PK_MOD;
        cmv.pad_arg[n] = (uint8_t)j;
        cmv_mod(j, 1);
        return;
    }
    if (r >= 2) {                                  /* a memory */
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
static void cmv_wheel_show(const char *t, const char *v)
{
    char s[48];
    cmv_cpy(cmv.wheel_t, t, sizeof cmv.wheel_t);
    cmv_cpy(cmv.wheel_v, v, sizeof cmv.wheel_v);
    cmv.wheel_until = cmv_now() + CMV_POP_MS;
    cmv.pop_kind = -1;
    cmv.msg_until = 0;
    cmv_cpy(s, t, sizeof s);
    cmv_cat(s, " ", sizeof s);
    cmv_cat(s, v, sizeof s);
    cmv_say(s);
}
static void cmv_wheel_sound(void)
{
    char nm[13];
    pool_name(trk[CR_PART_CHORD].eng_req % NENGINES, cs.sound, nm);
    cmv_wheel_show(ENGINES[trk[CR_PART_CHORD].eng_req % NENGINES]->name, nm);
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
    case RFM1_TK_CC_LOOP:
        if (on) {
            cu_latch_toggle();
            cmv_msg("Latch", cs.sticky ? "on" : "off");
        }
        break;
    case RFM1_TK_CC_COPY:
        if (on) {
            cu_lock_tap();
            cmv_msg("Lock", cu.clock ? "on" : "off");
        }
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
            if (!cmv.del_used) {                   /* tapped alone: PANIC */
                uint32_t n;
                for (n = 0; n < 32u; n++)          /* (what the pads hold ends nothing more) */
                    if (cmv.pad_kind[n] == PK_ROOT || cmv.pad_kind[n] == PK_PTRIG)
                        cmv.pad_kind[n] = PK_NONE;
                cmv.keys_down = 0;
                cu_mroot_allup();
                cu_panic();
                cmv_msg("PANIC", "all notes off");
            }
        }
        break;
    case RFM1_TK_CC_PLAY:
        if (on) {
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
    case RFM1_TK_CC_JOG_CLICK:                     /* the wheel's click: the chord sound's favourite mark */
        if (on) {
            int was = cu_fav_part(0);
            if (cu_fav_turn(0, was ? -1 : 1))
                cmv_wheel_show("Favorite", was ? "off" : "on");
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
    if (cmv.shift) {                               /* Shift + the wheel: the chord part's engine (OPT + PRESETS) */
        cmv.shift_used = 1;
        cu_epk_step(0, d);
        cu_epk_commit();
        cmv_wheel_show("Engine", ENGINES[trk[CR_PART_CHORD].eng_req % NENGINES]->name);
        return;
    }
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
                if (cmv.shift) {                   /* Shift + a step: a loop slot (1..10), a page (13..16) */
                    cmv.shift_used = 1;
                    if (i < CRL_SLOTS) {
                        char t[8];
                        if (i != cs.loop_slot)
                            cu_loop_load(i, 0);
                        cmv_mirror_msg();
                        cmv_int(t, (int32_t)i + 1, 0, sizeof t);
                        if ((int32_t)(cmv.msg_until - cmv_now()) <= 0)
                            cmv_msg("Slot", t);
                    } else if (i >= 12u) {
                        char t[8];
                        cmv.page = (uint8_t)(i - 12u);
                        cmv_cpy(t, "Page ", sizeof t);
                        cmv_int(t + 5, cmv.page + 1, 0, 3);
                        cmv_say(t);
                    }
                } else
                    cmv_step_press(i);
            } else if (cmv.step >= 0 && cmv.step_btn == i)
                cmv_step_release((uint32_t)cmv.step);
        } else if (d < 8u) {
            cmv_knob_touch(d, on);
        } else if (d == RFM1_TK_NOTE_JOG_TOUCH) {
            cmv.wheel_touch = (uint8_t)on;
            if (on)
                cmv_wheel_sound();
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
static void cmv_key_str(char *d, uint32_t n)       /* the header's key: "C MAJ" (Key Mode off: "FREE") */
{
    char sc[12];
    if (!cs.key_on) {
        cmv_cpy(d, "FREE", n);
        return;
    }
    cmv_cpy(d, CU_NOTE[cs.tonic % 12u], n);
    cmv_cpy(sc, cr_scale_name(cs.scale), sizeof sc);
    sc[3] = 0;
    cmv_upper(sc);
    cmv_cat(d, " ", n);
    cmv_cat(d, sc, n);
}
static void cmv_scr_strip(int play)                /* the page's 16 steps: filled = a chord; the playhead under */
{
    uint32_t i, ns = cmv_nsteps();
    for (i = 0; i < 16u; i++) {
        int x = 1 + (int)i * 8, s = (int)(cmv.page * 16u + i);
        if (cmv_step_has(i))
            px_rect(x, 52, 7, 7, 1);
        else
            px_box(x, 52, 7, 7, 1, (uint32_t)s >= ns);
        if (s == play)
            px_rect(x, 61, 7, 3, 1);
    }
}
static void cmv_scr_name(const char *nm, int y)
{
    int k = px_fit(nm, 4);
    px_text(1, y + (4 - k) * 3, nm, k, 1);
}
static void cmv_scr_home(void)
{
    char key[12], right[8], nm[24];
    const cr_chord_info_t *ci = &cr_snap.ci;
    cmv_key_str(key, sizeof key);
    cmv_cpy(right, "SEQ ", sizeof right);
    cmv_int(right + 4, cs.loop_slot + 1, 0, 4);
    px_header(key, right);
    if (ci->valid)
        cmv_chord_str(nm, sizeof nm, ci->root, ci->qual, ci->sup);
    else
        cmv_cpy(nm, "-", sizeof nm);
    cmv_scr_name(nm, 19);
    {
        int ph = cmv_playhead();
        cmv_scr_strip(ph >= 0 && (uint32_t)ph / 16u == cmv.page ? ph : -1);
    }
}
static void cmv_scr_arp(void)                      /* an arpeggio running: the notes, the one sounding on a block */
{
    char key[12], nm[24], rt[8];
    const cr_chord_info_t *ci = &cr_snap.ci;
    uint32_t i, n = ci->nnotes > 8u ? 8u : ci->nnotes;
    cmv_key_str(key, sizeof key);
    px_header(key, "ARP");
    cmv_chord_str(nm, sizeof nm, ci->root, ci->qual, ci->sup);
    px_text(1, 19, nm, px_tw(nm, 3) <= 90 ? 3 : 2, 1);
    cmv_cpy(rt, CU_DIV[cs.par[CR_PM_ARP][CR_P_DIV] % CR_DIV_COUNT], sizeof rt);
    px_text(127 - px_tw(rt, 2), 19, rt, 2, 1);
    for (i = 0; i < n; i++) {
        char nb[5];
        int x = 10 + (int)(i * (120u / (n ? n : 1u)));   /* (four notes: 10 40 70 100, as the design) */
        cr_note_name(ci->notes[i], nb);
        nb[(nb[1] == '#') ? 2 : 1] = 0;            /* (the name without the octave) */
        if ((int)i == cr_snap.perf_pos) {
            px_rect(x - 4, 38, 22, 22, 1);
            px_text(x + 2, 42, nb, 2, 0);
        } else
            px_text(x + 2, 50, nb, 2, 1);
    }
    px_rect(0, 61, 128, 1, 1);
}
static void cmv_scr_prog(void)                     /* the Prog layout: the progression's 16 chords of the page */
{
    char nm[CRPG_NAME + 1], right[8], cn[24];
    uint32_t i, maxc;
    int last = cmv.prog_last;
    crp_name(cs.prog_sel, nm);
    if (!nm[0]) {
        cmv_cpy(nm, "PROG ", sizeof nm);
        cmv_int(nm + 5, cs.prog_sel + 1, 0, 4);
    }
    cmv_upper(nm);
    right[0] = 0;
    if (last >= 0)
        cmv_int(right, last + 1, 0, sizeof right);
    maxc = (uint32_t)(127 - px_tw(right, 2) - 4 + 2) / 12u;
    if (maxc < sizeof nm && nm[maxc])
        nm[maxc] = 0;
    px_header(nm, right);
    cn[0] = 0;
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
            cmv_chord_str(cn, sizeof cn, r, q, sup);
        }
    }
    if (!cn[0])
        cmv_cpy(cn, "-", sizeof cn);
    cmv_scr_name(cn, 19);
    for (i = 0; i < 16u; i++) {
        uint8_t nt[CRPG_NOTES];
        uint32_t key = cmv.prog_page * 16u + i;
        int x = 1 + (int)i * 8;
        if (crp_chord(cs.prog_sel, key, nt))
            px_rect(x, 52, 7, 7, 1);
        else
            px_box(x, 52, 7, 7, 1, 1);
        if ((int)key == last)
            px_rect(x, 61, 7, 3, 1);
    }
}
static void cmv_scr_slots(void)                    /* Shift held: the loop slots, the page */
{
    char pg[8];
    uint32_t i;
    cmv_cpy(pg, "PG ", sizeof pg);
    cmv_int(pg + 3, cmv.page + 1, 0, 4);
    px_header("SLOTS", pg);
    for (i = 0; i < CRL_SLOTS; i++) {
        char t[4];
        int x = 1 + (int)(i % 5u) * 25, y = 20 + (int)(i / 5u) * 22;
        int full = (int)(((cs.loop_used >> i) & 1u) || (i == cs.loop_slot && cr_snap.llen));
        cmv_int(t, (int32_t)i + 1, 0, sizeof t);
        if (i == cs.loop_slot) {
            px_rect(x, y, 24, 20, 1);
            px_ctext(x + 12, y + 3, t, 2, 0);
        } else {
            px_box(x, y, 24, 20, 1, !full);
            px_ctext(x + 12, y + 3, t, 2, 1);
        }
    }
}
static void cmv_scr_layout(void)                   /* Menu held: the picker */
{
    char bk[8], pv[16];
    cmv_cpy(bk, "BK ", sizeof bk);
    cmv_int(bk + 3, cmv.bank + 1, 0, 4);
    px_header("LAYOUT", bk);
    px_ctext(64, 19, CMV_LAYOUT[cmv.layout], 4, 1);
    cmv_cpy(pv, "prev ", sizeof pv);
    cmv_cat(pv, CMV_LAYOUT[cmv.prev], sizeof pv);
    px_text(1, 50, pv, 2, 1);
}
static void cmv_scr_step(void)                     /* a step held: its chord, its length and chance */
{
    char t[12], rt[8], nm[24], ln[16], ch[8];
    cmv_cpy(t, "STEP ", sizeof t);
    cmv_int(t + 5, cmv.step + 1, 0, 6);
    cmv_cpy(rt, cu_le.cnt ? CU_LE_RAT[cu_le.rat <= 0 ? 0 : cu_le.rat & 3] : "", sizeof rt);
    if (cu_le.cnt && cu_le.rat <= 0)
        cmv_cpy(rt, "x1", sizeof rt);
    px_header(t, rt);
    if (cu_le.cnt) {
        char r[4], q[5], sup[CR_SUP_MAX];
        cu_ev_name(cu_le.root, cu_le.qx, r, q, sup);
        cmv_chord_str(nm, sizeof nm, r, q, sup);
        cmv_scr_name(nm, 19);
        cmv_cpy(ln, "len ", sizeof ln);
        if (cu_le.nf)
            cmv_cat(ln, CU_LE_FRAC[cu_le.nf & 3u], sizeof ln);
        else
            cmv_int(ln + 4, cu_le.n, 0, sizeof ln - 4u);
        px_text(1, 50, ln, 2, 1);
        cmv_int(ch, CRL_CHANCE_PCT(cu_le.chance < 0 ? 0 : cu_le.chance), 0, sizeof ch);
        cmv_cat(ch, "%", sizeof ch);
        px_text(127 - px_tw(ch, 2), 50, ch, 2, 1);
    } else {
        cmv_scr_name("-", 19);
        px_text(1, 50, "empty", 2, 1);
    }
}
static void cmv_scr_popup(const char *title, const char *value, int32_t pct)   /* full screen: the name, the value */
{
    px_ctext(64, 2, title, px_fit(title, 2), 1);
    {
        int k = px_fit(value, 4);
        px_ctext(64, 24 + (4 - k) * 3, value, k, 1);
    }
    if (pct >= 0) {
        px_box(8, 58, 112, 5, 1, 0);
        px_rect(9, 59, (int)(110 * (pct > 1000 ? 1000 : pct) / 1000), 3, 1);
    }
}
static void cmv_scr_saved(void)
{
    char t[16];
    cmv_cpy(t, "Memory ", sizeof t);
    cmv_int(t + 7, cmv.saved_n, 0, 4);
    px_ctext(64, 0, t, 2, 1);
    px_ctext(64, 17 + (4 - px_fit(cmv.saved_name, 4)) * 3, cmv.saved_name, px_fit(cmv.saved_name, 4), 1);
    px_ctext(64, 50, "saved", 2, 1);
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
    else if ((int32_t)(cmv.msg_until - now) > 0)
        cmv_scr_popup(cmv.msg_t, cmv.msg_v, -1);
    else if (cmv.pop_kind >= 0 && (cmv.pop_touch || (int32_t)(cmv.pop_until - now) > 0)) {
        char v[24];
        int32_t pct;
        cmv_value(cmv.pop_kind, v, sizeof v, &pct);
        cmv_scr_popup(CMV_KNAME[cmv.pop_kind], v, pct);
    } else if (cmv.wheel_touch || (int32_t)(cmv.wheel_until - now) > 0)
        cmv_scr_popup(cmv.wheel_t, cmv.wheel_v, -1);
    else if (cmv.step >= 0)
        cmv_scr_step();
    else if (cmv.menu && (cmv.menu_used || now - cmv.menu_t0 >= CMV_HOLD_MS))
        cmv_scr_layout();
    else if (cmv.shift)
        cmv_scr_slots();
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
    uint16_t sc = cs.key_on ? cr_scale_bits(cs.tonic, cs.scale) : cr_scale_bits(0, CR_SCALE_MAJOR);
    uint32_t ton = cs.key_on ? cs.tonic : 0u;
    const int picker = cmv.menu && (cmv.menu_used || now - cmv.menu_t0 >= CMV_HOLD_MS);
    for (r = 0; r < 4; r++)
        for (c = 0; c < 8; c++) {
            const int n = cmv_pad(r, c), held = (int)((cmv.pad_held >> n) & 1u);
            if (picker) {                          /* layout E */
                if (r == 3 && c < 4)
                    c == cmv.layout ? led(n, C_WHITE) : led_dim(n, C_WHITE, c == cmv.prev ? 45u : 15u);
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
            if (r >= 2 && c < 4) {                 /* the chord keys */
                uint32_t j = (uint32_t)((3 - r) * 4 + c);
                if (held)
                    led(n, C_WHITE);
                else if ((cu.mlatch >> j) & 1u)
                    j < 4u ? led(n, C_VIOLET) : led(n, C_ORANGE);
                else
                    j < 4u ? led(n, C_VIOLET_D) : led(n, C_ORANGE_D);
                continue;
            }
            if (r >= 2) {                          /* the memories */
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
                else if (held)
                    led(n, C_WHITE);
                else if (cmv.layout == CMV_KEYS)
                    note % 12 == 0 ? led(n, C_YELLOW) : led(n, C_KEY_D);
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
    if (cmv.shift) {                               /* layout D: the slots, the pages */
        for (i = 0; i < 16u; i++) {
            const int L = RFM1_TK_L_STEP0 + (int)i;
            if (i < CRL_SLOTS) {
                int full = (int)(((cs.loop_used >> i) & 1u) || (i == cs.loop_slot && cr_snap.llen));
                i == cs.loop_slot ? led(L, C_HELD) : led_dim(L, C_SEQ, full ? 60u : 12u);
            } else if (i >= 12u)
                i - 12u == cmv.page ? led(L, C_WHITE) : led_dim(L, C_WHITE, 25u);
            else
                led(L, 0, 0, 0);
        }
    } else {
        ns = cmv_nsteps();
        for (i = 0; i < 16u; i++) {
            const int L = RFM1_TK_L_STEP0 + (int)i, s = (int)(cmv.page * 16u + i);
            if (s == cmv.step)
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
    for (i = 0; i < 16u; i++)                      /* the icons under the steps: the page */
        rfm1_tk_white(RFM1_TK_L_ICON0 + (int)i, i < 4u ? (i == cmv.page ? 255 : 64) : 0);
}
static void cmv_leds(void)
{
    static const uint8_t RING[3][8][3] = {
        {{C_ORANGE}, {C_VIOLET}, {C_VIOLET}, {C_VIOLET}, {C_BASS}, {C_YELLOW}, {C_YELLOW}, {C_YELLOW}},
        {{C_WHITE}, {C_BASS}, {C_VIOLET}, {C_VIOLET}, {C_BASS}, {C_PROG}, {C_PROG}, {C_WHITE}},
        {{C_HELD}, {C_HELD}, {C_HELD}, {C_HELD}, {C_HELD}, {C_HELD}, {C_HELD}, {C_SEQ}}};
    const uint32_t now = cmv_now();
    const int pg = cmv.step >= 0 ? 2 : cmv.shift ? 1 : 0;
    const int blink = cu_blink(now);
    uint32_t k;
    cmv_led_pads();
    cmv_led_steps();
    for (k = 0; k < 8u; k++)
        led(RFM1_TK_L_KNOB1 + (int)k, RING[pg][k][0], RING[pg][k][1], RING[pg][k][2]);
    cu_playing() ? led(RFM1_TK_L_PLAY, 0, 200, 0) : led(RFM1_TK_L_PLAY, 0, 40, 0);
    if (cr_snap.lcap == CRL_CAP_REC || cr_snap.lcap == CRL_CAP_OD || cr_snap.lcap == CRL_CAP_COUNTIN)
        led(RFM1_TK_L_REC, blink ? 255 : 60, 0, 0);
    else if (cr_snap.lcap != CRL_CAP_NONE)
        led(RFM1_TK_L_REC, 255, 0, 0);
    else
        led(RFM1_TK_L_REC, 0x44, 0, 0);
    led(RFM1_TK_L_SAMPLE, 0, 0, 0);
    rfm1_tk_white(RFM1_TK_L_SHIFT, cmv.shift ? 255 : 60);
    rfm1_tk_white(RFM1_TK_L_MENU, cmv.menu ? 255 : 77);
    rfm1_tk_white(RFM1_TK_L_BACK, 77);
    rfm1_tk_white(RFM1_TK_L_CAPTURE, 77);
    rfm1_tk_white(RFM1_TK_L_DOWN, cu.octave < 0 ? 255 : 77);
    rfm1_tk_white(RFM1_TK_L_UP, cu.octave > 0 ? 255 : 77);
    rfm1_tk_white(RFM1_TK_L_UNDO, 77);
    rfm1_tk_white(RFM1_TK_L_LOOP, cs.sticky ? 255 : 77);
    rfm1_tk_white(RFM1_TK_L_COPY, cu.clock ? 255 : 77);
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
    if (cmv.step >= 0 && !cu.ledit)                /* (the editor closed under the hold: the FM-1, a capture) */
        cmv.step = -1, cmv.step_ed = 0;
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
        cmv.shift = cmv.menu = cmv.del = 0;
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
    if (cmv.layout == CMV_SCALE)
        cmv.layout = CMV_KEYS;
    rfm1_tk_register("CHORDS", RFM1_TK_ALL & ~(RFM1_TK_TRACKS | RFM1_TK_VOLUME), &CMV_OPS);
    cmv_pads_sync();
    cmv_px = rfm1_tk_screen();
    if (cmv_px) {
        cmv_draw();
        cmv_leds();
    }
}
