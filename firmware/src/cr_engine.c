/* SPDX-License-Identifier: GPL-3.0-only
 * ChoralRoot FM-1: the chord / performance engine, ported from choralroot's d_cr_engine.lua (grid iii).
 * Chord tables with their firmware provenance, KEYMAP harmonic quantization, voicing rotation, the
 * Simple / Advanced / Free play styles, Extension Addition, bass behaviours, the note-ownership registry,
 * the Sticky / Hold latch, chord pads, panic, and the strum / slop / arp / pattern / harp scheduler with
 * BPM rephasing. Integer only, no allocation, no libc: one cr_t holds everything. CR_ENGINE.md has the
 * API, the timing model and the provenance of every table. Design references are to choralroot design.md. */
#include "cr_engine.h"

/* ============================================================ tables === */

/* Chord intervals: low nibble = count, following nibbles = semitones. dim/min/maj/sus + the 3 Secret
 * types (aug, power, min4 = [0,3,5]) are firmware ground truth (fw 3.63 -> 3.84 diff;
 * ORCHID_FIRMWARE_REFERENCE.md 6.1/7). Indexed by CR_Q_*. */
static const uint16_t CR_QUALITY[CR_Q_COUNT] = { 0, 0x6303, 0x7303, 0x7403, 0x7503, 0x8403, 0xC703, 0x5303 };

/* Extension intervals in mask-bit order 6, m7, M7, 9 (ORCHID_FIRMWARE_REFERENCE.md 6.2, high-confidence). */
static const uint8_t CR_EXT_IV[4] = { 9, 10, 11, 14 };

/* Key Mode rule table (design.md 10.3/10.4): one byte per pitch class, relative to the tonic. Low 3 bits =
 * quality (1 dim, 2 min, 3 maj, 4 sus); bits 3-4 = root shift (0 none, 8 down, 16 up). Diatonic degrees keep
 * their triad; chromatic degrees resolve to a neighbour as SUS (C# in C -> Csus, measured; the rest a labelled
 * fallback: lower neighbour, upward where its sus4 leaves the scale). */
static const uint8_t CR_KEYMAP[2][12] = {
    { 3, 12, 2, 12, 2, 3, 20, 3, 12, 2, 12, 1 },   /* major */
    { 2, 12, 1, 3, 12, 2, 12, 2, 3, 20, 3, 12 },   /* minor */
};

/* Divisions in 1/24-beat units (2/1 .. 1/32T), the order of cr_div_t (design.md 12.3). */
static const uint8_t CR_DIV_UNITS[CR_DIV_COUNT] = { 192, 96, 48, 32, 24, 16, 12, 8, 6, 4, 3, 2 };

/* Pattern slots (design.md 12.4): bytes 1-13 = 1-based start offsets of patterns 1-12 (byte 13 ends p12),
 * then one byte per step: low nibble = chord-note index (0 = rest), high nibble = accent L,
 * vel = trigger * (L+1) / 16. 12 Choralroot-designed phrases (ORCHID factory data unpublished; none is
 * factory parity until measured from hardware). */
static const uint8_t CR_PATTERNS[88] = {
    0x0E, 0x12, 0x16, 0x19, 0x1D, 0x23, 0x2B, 0x33, 0x39, 0x41, 0x49, 0x51, 0x59,
    0xF1, 0x82, 0xB3, 0x84,                                 /* 1 pulse */
    0xF1, 0x83, 0xB2, 0x83,                                 /* 2 alberti */
    0xF1, 0x93, 0x92,                                       /* 3 waltz */
    0xF4, 0x93, 0xB2, 0x91,                                 /* 4 cascade */
    0xF1, 0x82, 0x93, 0xF4, 0x83, 0x92,                     /* 5 updown */
    0xF1, 0x00, 0xC2, 0x93, 0x00, 0x81, 0xC3, 0x00,         /* 6 syncopate */
    0xF1, 0x31, 0xC2, 0x31, 0xF3, 0x31, 0xC4, 0x31,         /* 7 gallop */
    0xF1, 0x93, 0xB2, 0x94, 0xB3, 0x95,                     /* 8 skip */
    0xF1, 0x00, 0x00, 0x93, 0xC2, 0x00, 0x83, 0x00,         /* 9 ballad */
    0xF1, 0x00, 0xB3, 0x00, 0xC2, 0x00, 0xB4, 0x00,         /* 10 stride */
    0xF1, 0x32, 0x81, 0x33, 0xC1, 0x34, 0x81, 0x32,         /* 11 drive */
    0xF1, 0x82, 0x31, 0xB3, 0x31, 0xC4, 0x31, 0xB5,         /* 12 ladder */
};
static const char *const CR_PATTERN_NAMES[CR_NPATTERN] = {
    "pulse", "alberti", "waltz", "cascade", "updown", "syncopate",
    "gallop", "skip", "ballad", "stride", "drive", "ladder",
};

/* Performance defaults (design.md 21), columns in cr_param_t order:
 *                 RATE DIV          DIR RANGE GATE SWING RETRIG PATTERN ROTATE AMOUNT HOLD */
static const int16_t CR_PAR_DEFAULT[CR_PM_COUNT][CR_P_COUNT] = {
    /* strum   */ { 40, CR_DIV_1_8, 0, 1, 100, 50, 1, 1, 0, 0, 0 },
    /* slop    */ { 40, CR_DIV_1_8, 0, 1, 100, 50, 1, 1, 0, 30, 0 },
    /* arp     */ { 40, CR_DIV_1_8, 0, 1, 70, 50, 1, 1, 0, 0, 0 },     /* Hold default off: LOCK latches */
    /* pattern */ { 40, CR_DIV_1_8, 0, 1, 70, 50, 1, 1, 0, 0, 0 },
    /* harp    */ { 8, CR_DIV_1_8, 0, 3, 100, 50, 1, 1, 0, 0, 0 },
};
static const int16_t CR_PAR_MIN[CR_P_COUNT] = { 1, 0, 0, 1, 1, 50, 0, 1, -12, 0, 0 };
static const int16_t CR_PAR_MAX[CR_P_COUNT] = { 1000, CR_DIV_COUNT - 1, CR_DIR_RANDOM, 4, 200, 90, 1, CR_NPATTERN, 12, 100, 1 };

/* Orchid Standard Chord Naming (manual 6; label strings ORCHID_FIRMWARE_REFERENCE.md 6.4). */
static const char *const CR_NOTE_NAMES[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
static const char *const CR_QUAL_NAMES[CR_Q_COUNT] = { "", "dim", "m", "", "sus", "+", "", "m" };   /* pow / min4: sup "5" / "add4" */

#define CR_VOICE_MIN (-12)       /* chord voicing rotation clamp (design.md 8) */
#define CR_VOICE_MAX 12
#define CR_BV_MIN (-2)           /* bass register, octaves from C2 (design.md 9) */
#define CR_BV_MAX 4
#define CR_BASS_OCT 2
#define CR_BLOCK_UNITS 384u      /* 16 beats in 1/24-beat units: a whole number of every division's pair */
#define CR_NONE 0xFFu

enum { CR_EV_NOTE_OFF = 1, CR_EV_STRUM, CR_EV_CLOCK };
#define CR_OWN_V(vi) ((uint8_t)((unsigned)(vi) * 2u))        /* a voice's strum / note-off events */
#define CR_OWN_C(vi) ((uint8_t)((unsigned)(vi) * 2u + 1u))   /* its clocked-step events */

/* =========================================================== helpers === */
static void crx_zero(void *p, uint32_t n)
{
    uint8_t *d = (uint8_t *)p;
    while (n--) *d++ = 0;
}
static int crx_mod(int a, int m)
{
    int r = a % m;
    return r < 0 ? r + m : r;
}
static int crx_clamp(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }
static uint32_t crx_rand(cr_t *c)
{
    uint32_t x = c->rng ? c->rng : 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    c->rng = x;
    return x;
}
static int crx_randn(cr_t *c, int n) { return n > 0 ? (int)(crx_rand(c) % (uint32_t)n) : 0; }
static void crx_sort(uint8_t *a, int n)
{
    int i, j;
    for (i = 1; i < n; i++) {
        uint8_t x = a[i];
        for (j = i; j > 0 && a[j - 1] > x; j--) a[j] = a[j - 1];
        a[j] = x;
    }
}
static int crx_bits(unsigned x)
{
    int n = 0;
    while (x) { n += (int)(x & 1u); x >>= 1; }
    return n;
}

/* ============================================== output + note registry === */
static void crx_out_on(cr_t *c, int s, int note, int vel)
{
    if (c->out.note_on) c->out.note_on(c->out.ud, (cr_stream_t)s, (uint8_t)note, (uint8_t)vel);
}
static void crx_out_off(cr_t *c, int s, int note)
{
    if (c->out.note_off) c->out.note_off(c->out.ud, (cr_stream_t)s, (uint8_t)note);
}

/* refcounted per (stream, note) (design.md 17): a shared note retriggers, the last owner ends it */
static int crx_note_start(cr_t *c, int s, int note, int vel)
{
    uint8_t *r;
    if (note < 0 || note > 127) return 0;
    vel = crx_clamp(vel, 1, 127);
    r = &c->reg[s][note];
    if (*r) {
        crx_out_off(c, s, note);   /* retrigger a shared note */
        crx_out_on(c, s, note, vel);
        if (*r < 255u) (*r)++;
    } else {
        crx_out_on(c, s, note, vel);
        *r = 1;
    }
    return 1;
}
static void crx_note_end(cr_t *c, int s, int note)
{
    uint8_t *r;
    if (note < 0 || note > 127) return;
    r = &c->reg[s][note];
    if (!*r) return;
    if (*r <= 1u) { *r = 0; crx_out_off(c, s, note); }
    else (*r)--;
}

/* ========================================================= scheduler === */
/* Owner-cancellable pooled queue, ascending by due; ties keep insertion order (same-tick FIFO). Every
 * delay is >= 1 ms, so an event scheduled while the queue drains never joins the drained prefix. */
static int crx_room(const cr_t *c, int n) { return (int)c->nev + n <= CR_MAX_EV; }
static int crx_sched(cr_t *c, uint32_t delay, int kind, uint8_t owner, int vi, int note)
{
    uint32_t due;
    int i;
    if (c->nev >= CR_MAX_EV) { c->ev_overflow++; return 0; }
    if (delay < 1u) delay = 1u;
    due = c->now + delay;
    i = c->nev;
    while (i > 0 && (int32_t)(c->ev[i - 1].due - due) > 0) { c->ev[i] = c->ev[i - 1]; i--; }
    c->ev[i].due = due;
    c->ev[i].kind = (uint8_t)kind;
    c->ev[i].owner = owner;
    c->ev[i].vi = (uint8_t)vi;
    c->ev[i].note = (uint8_t)note;
    c->nev++;
    return 1;
}
static void crx_cancel(cr_t *c, uint8_t owner)
{
    int i, j = 0;
    for (i = 0; i < c->nev; i++)
        if (c->ev[i].owner != owner) c->ev[j++] = c->ev[i];
    c->nev = (uint16_t)j;
}

/* ====================================================== chord tables === */
int cr_chord_base(int root_note, uint8_t quality, uint8_t ext, uint8_t out[CR_CHORD_MAX])
{
    uint8_t iv[CR_CHORD_MAX];
    int ni = 0, n = 0, j, k;
    if (quality > CR_Q_NONE && quality < CR_Q_COUNT) {
        uint16_t q = CR_QUALITY[quality];
        for (j = 1; j <= (int)(q & 15u); j++) iv[ni++] = (uint8_t)((q >> (j * 4)) & 15u);
        for (k = 0; k < 4; k++) {   /* extensions need a Chord Type (or Key Mode) */
            int dup = 0;
            if (!(ext & (1u << k))) continue;
            for (j = 0; j < ni; j++) if (iv[j] == CR_EXT_IV[k]) dup = 1;
            if (!dup && ni < CR_CHORD_MAX) iv[ni++] = CR_EXT_IV[k];
        }
    } else {
        iv[ni++] = 0;   /* single note */
    }
    crx_sort(iv, ni);
    for (j = 0; j < ni; j++) {
        int nn = root_note + iv[j];
        if (nn >= 0 && nn <= 127) out[n++] = (uint8_t)nn;
    }
    return n;
}

/* voicing: lowest note +12 / highest -12, k steps from the unvoiced base; a step leaving 0..127 stops
 * the rotation for that chord (design.md 8) */
int cr_voice_apply(const uint8_t *base, int n, int k, uint8_t out[CR_CHORD_MAX])
{
    int i;
    for (i = 0; i < n; i++) out[i] = base[i];
    crx_sort(out, n);
    if (!n) return 0;
    for (; k > 0; k--) {
        if (out[0] + 12 > 127) break;
        out[0] = (uint8_t)(out[0] + 12);
        crx_sort(out, n);
    }
    for (; k < 0; k++) {
        if (out[n - 1] < 12) break;
        out[n - 1] = (uint8_t)(out[n - 1] - 12);
        crx_sort(out, n);
    }
    return n;
}

/* Secret Chords (design.md 6.3): the two most-recently-held Chord Types map to a secret quality when the
 * scope allows it for this Play Style, else 0. Types firmware-confirmed; the combo map is a Choralroot
 * choice (provisional: min+sus -> min4 inferred). Modifiers stack normally; nothing is consumed. */
static int crx_secret_quality(const cr_t *c)
{
    int i, a = 0, b = 0;
    if (c->secret == CR_SECRET_OFF || (c->secret == CR_SECRET_SIMPLE && c->playstyle != CR_PS_SIMPLE)) return 0;
    for (i = (int)c->qn - 1; i >= 0; i--) {
        int id = c->qstack[i];
        if (!(c->mods_down & (1u << (id - 1)))) continue;
        if (!a) a = id;
        else if (id != a) { b = id; break; }
    }
    if (!b) return 0;
    if (a == CR_Q_SUS || b == CR_Q_SUS) {
        int o = a == CR_Q_SUS ? b : a;
        if (o == CR_Q_DIM) return CR_Q_POW;
        if (o == CR_Q_MAJ) return CR_Q_AUG;
        if (o == CR_Q_MIN) return CR_Q_MIN4;
    }
    return 0;   /* Dim+Min, Dim+Maj, Min+Maj: unmapped, last-pressed wins */
}

/* chord spec captured at note-on: the last-pressed held quality (or a secret), the held extensions */
static void crx_snapshot_spec(const cr_t *c, uint8_t *sq, uint8_t *sx)
{
    int i, q = 0, s;
    for (i = (int)c->qn - 1; i >= 0; i--)
        if (c->mods_down & (1u << (c->qstack[i] - 1))) { q = c->qstack[i]; break; }
    s = crx_secret_quality(c);
    *sq = (uint8_t)(s ? s : q);
    *sx = (uint8_t)((c->mods_down >> 4) & 15u);
}

/* Key Mode resolves root/quality via KEYMAP; a held quality overrides (design.md 10). Fills the voice's
 * resolved quality, root, bass pitch class and base notes (transposed by the voice's captured transpose). */
static void crx_build_base(const cr_t *c, cr_voice_t *v)
{
    int q = v->sq, root = v->note_pressed, pc = v->pc_pressed;
    if (c->key_on) {
        int e = CR_KEYMAP[c->scale][crx_mod(v->pc_pressed - c->tonic, 12)], sh = e >> 3;
        if (sh == 1) { root--; pc = crx_mod(pc - 1, 12); }
        else if (sh == 2) { root++; pc = crx_mod(pc + 1, 12); }
        if (!q) q = e & 7;
    }
    if (!q && c->single == CR_SINGLE_SPLIT && v->pc_pressed < c->split_pc && root >= 12)
        root -= 12;                  /* Single Notes Split: below the split point, an octave lower */
    v->rq = (uint8_t)q;
    v->root = (int16_t)root;
    v->pc = (uint8_t)pc;
    v->nbase = (uint8_t)cr_chord_base(root + v->transpose, (uint8_t)q, v->sx, v->base);
}

/* ======================================================= performance === */
static int crx_expand_range(const uint8_t *notes, int n, int range, uint8_t *out)
{
    uint8_t seen[16];
    int o, i, k = 0;
    crx_zero(seen, sizeof seen);
    for (o = 0; o < range; o++)
        for (i = 0; i < n; i++) {
            int nn = notes[i] + o * 12;
            if (nn <= 127 && !(seen[nn >> 3] & (1u << (nn & 7)))) {
                seen[nn >> 3] |= (uint8_t)(1u << (nn & 7));
                out[k++] = (uint8_t)nn;
            }
        }
    crx_sort(out, k);
    return k;
}

/* Note order for one-shot (strum/slop/harp) and cyclic (arp). Cyclic updown omits the repeated low end;
 * played keeps the voiced order per octave; random is per-step for the arp (fire time). */
static int crx_order_notes(cr_t *c, const uint8_t *sorted, int n, int dir, int cyclic,
                           const uint8_t *played, int np, int range, uint8_t *seq)
{
    int i, k = 0, o;
    if (dir == CR_DIR_DOWN) {
        for (i = n - 1; i >= 0; i--) seq[k++] = sorted[i];
    } else if (dir == CR_DIR_UPDOWN) {
        for (i = 0; i < n; i++) seq[k++] = sorted[i];
        for (i = n - 2; i >= (cyclic ? 1 : 0); i--) seq[k++] = sorted[i];
    } else if (dir == CR_DIR_DOWNUP) {
        for (i = n - 1; i >= 0; i--) seq[k++] = sorted[i];
        for (i = 1; i <= n - 2; i++) seq[k++] = sorted[i];
    } else if (dir == CR_DIR_PLAYED) {
        for (o = 0; o < range; o++)
            for (i = 0; i < np; i++) {
                int nn = played[i] + o * 12;
                if (nn <= 127 && k < CR_SEQ_MAX) seq[k++] = (uint8_t)nn;
            }
    } else if (dir == CR_DIR_RANDOM && !cyclic) {
        for (i = 0; i < n; i++) seq[k++] = sorted[i];
        for (i = n - 1; i >= 1; i--) {
            int j = crx_randn(c, i + 1);
            uint8_t t = seq[i]; seq[i] = seq[j]; seq[j] = t;
        }
    } else {
        for (i = 0; i < n; i++) seq[k++] = sorted[i];
    }
    return k;
}

static void crx_own_append(cr_own_t *o, int note) { if (o->n < CR_OWN_MAX) o->note[o->n++] = (uint8_t)note; }

/* scheduler dispatch targets; a stale event on a hushed voice is a no-op */
static void crx_perf_note_off(cr_t *c, int vi, int note)
{
    cr_voice_t *v = &c->v[vi];
    int i;
    if (!v->used || !v->main.live) return;   /* voice hushed in the same tick */
    for (i = 0; i < v->main.n; i++)
        if (v->main.note[i] == note) {
            for (; i + 1 < v->main.n; i++) v->main.note[i] = v->main.note[i + 1];
            v->main.n--;
            crx_note_end(c, CR_STREAM_MAIN, note);
            return;
        }
}

/* one strum/slop/harp note; reads velocity and (harp) gate length off the voice */
static void crx_strum_fire(cr_t *c, int vi, int note)
{
    cr_voice_t *v = &c->v[vi];
    if (!v->used || !v->main.live) return;
    if (v->main.n >= CR_OWN_MAX || (v->gate_t && !crx_room(c, 1))) return;   /* never strand a note */
    if (crx_note_start(c, CR_STREAM_MAIN, note, v->vel)) {
        crx_own_append(&v->main, note);
        v->last_note = (uint8_t)note; v->has_last = 1;
        if (v->gate_t) crx_sched(c, v->gate_t, CR_EV_NOTE_OFF, CR_OWN_V(vi), vi, note);
    }
}

/* clocked modes (design.md 12.3/12.4/20): one step at a time, beat-accumulated (no drift). Step k of a
 * voice lands at anchor + pos(k), pos in 1/2400 beat = ((k/2)*200 + odd*2*swing) * div_units, and
 * 1/2400 beat = 25/bpm ms; anchor carries a remainder in 1/bpm ms so rounding never accumulates. */
static uint32_t crx_due_at(uint32_t anchor, uint32_t rem, uint32_t k, uint32_t du, uint32_t sw, uint32_t bpm)
{
    uint32_t pos = ((k >> 1) * 200u + (k & 1u) * 2u * sw) * du;
    uint32_t x = rem + pos * 25u;
    return anchor + (2u * x + bpm) / (2u * bpm);
}

static void crx_build_clocked_seq(cr_t *c, cr_voice_t *v)
{
    const int16_t *p = c->par[v->clk.mode];
    uint8_t ex[CR_OWN_MAX];
    int n = crx_expand_range(v->notes, v->nnotes, p[CR_P_RANGE], ex);
    if (v->clk.mode == CR_PM_ARP)
        v->clk.nseq = (uint8_t)crx_order_notes(c, ex, n, p[CR_P_DIR], 1, v->notes, v->nnotes, p[CR_P_RANGE], v->clk.seq);
    else {
        int i;
        for (i = 0; i < n; i++) v->clk.seq[i] = ex[i];
        v->clk.nseq = (uint8_t)n;
    }
}

static void crx_fire_clocked_once(cr_t *c, int vi)
{
    cr_voice_t *v = &c->v[vi];
    const int16_t *p = c->par[v->clk.mode];
    uint32_t du = CR_DIV_UNITS[p[CR_P_DIV]];
    int n = -1, vel = v->vel;
    if (v->clk.mode == CR_PM_ARP) {
        if (v->clk.nseq) {
            if (p[CR_P_DIR] == CR_DIR_RANDOM) n = v->clk.seq[crx_randn(c, v->clk.nseq)];
            else n = v->clk.seq[v->clk.pos % v->clk.nseq];
        }
    } else {
        int i = p[CR_P_PATTERN];
        uint32_t a = CR_PATTERNS[i - 1], len = (uint32_t)CR_PATTERNS[i] - a;   /* this pattern's byte range */
        int b = CR_PATTERNS[a - 1u + v->clk.pos % len], pi = b & 15;
        if (pi > 0 && v->clk.nseq) {
            n = v->clk.seq[crx_mod(pi - 1 + p[CR_P_ROTATE], v->clk.nseq)];
            vel = (vel * ((b >> 4) + 1) + 8) / 16;   /* accent scaling */
            if (vel < 1) vel = 1;
        }
    }
    if (n >= 0 && v->main.live && v->main.n < CR_OWN_MAX && crx_room(c, 1)) {
        if (crx_note_start(c, CR_STREAM_MAIN, n, vel)) {
            uint32_t bpm = c->bpm, dur = (2u * 25u * du * (uint32_t)p[CR_P_GATE] + bpm) / (2u * bpm);
            crx_own_append(&v->main, n);
            v->last_note = (uint8_t)n; v->has_last = 1;
            crx_sched(c, dur ? dur : 1u, CR_EV_NOTE_OFF, CR_OWN_V(vi), vi, n);
        }
    }
    v->clk.pos++;
    v->clk.step++;
}

/* fire every step that is due (a late tick catches up in order), then queue the next */
static void crx_clocked_step(cr_t *c, int vi)
{
    int guard;
    for (guard = 0; guard < 64; guard++) {
        cr_voice_t *v = &c->v[vi];
        const int16_t *p;
        uint32_t du, sw, bpm = c->bpm, due;
        if (!v->used || !v->clk.on) return;
        p = c->par[v->clk.mode];
        du = CR_DIV_UNITS[p[CR_P_DIV]];
        sw = (uint32_t)p[CR_P_SWING];
        if (v->clk.step >= 2u) {   /* fold whole swing pairs into the anchor (exact, keeps numbers small) */
            uint32_t m = v->clk.step >> 1, total = m * 5000u * du + v->clk.rem;
            v->clk.anchor += total / bpm;
            v->clk.rem = (uint16_t)(total % bpm);
            v->clk.step -= m * 2u;
        }
        due = crx_due_at(v->clk.anchor, v->clk.rem, v->clk.step, du, sw, bpm);
        if ((int32_t)(due - c->now) <= 0) { crx_fire_clocked_once(c, vi); continue; }
        crx_sched(c, due - c->now, CR_EV_CLOCK, CR_OWN_C(vi), vi, 0);
        return;
    }
    crx_sched(c, 1, CR_EV_CLOCK, CR_OWN_C(vi), vi, 0);   /* runaway catch-up: continue next tick */
}

/* retrig ON anchors at the trigger; OFF at the global grid (chords substitute into the running phase) */
static void crx_phase_clocked(cr_t *c, cr_voice_t *v, int setpos)
{
    const int16_t *p = c->par[v->clk.mode];
    if (p[CR_P_RETRIG]) {
        v->clk.anchor = c->now; v->clk.rem = 0; v->clk.step = 0;
    } else {
        uint32_t du = CR_DIV_UNITS[p[CR_P_DIV]], sw = (uint32_t)p[CR_P_SWING], bpm = c->bpm, k = 0;
        int32_t el = (int32_t)(c->now - c->clk_anchor);
        v->clk.anchor = c->clk_anchor; v->clk.rem = c->clk_rem;
        if (el > 0) {
            k = (uint32_t)el * bpm / (du * 2500u);
            k = k > 2u ? k - 2u : 0u;
        }
        while ((int32_t)(crx_due_at(v->clk.anchor, v->clk.rem, k, du, sw, bpm) - c->now) <= 0) k++;
        v->clk.step = k;
        if (setpos) v->clk.pos = k + c->clk_units / du;
    }
}

static void crx_rephase_clocked(cr_t *c, int vi)
{
    cr_voice_t *v = &c->v[vi];
    if (!v->used || !v->clk.on) return;
    crx_cancel(c, CR_OWN_C(vi));
    crx_phase_clocked(c, v, 0);
    crx_clocked_step(c, vi);
}

static void crx_start_clocked(cr_t *c, int vi)
{
    cr_voice_t *v = &c->v[vi];
    v->clk.on = 1;
    v->clk.mode = c->perform_mode;
    v->clk.pos = 0; v->clk.step = 0;
    v->clk.anchor = c->now; v->clk.rem = 0;
    crx_build_clocked_seq(c, v);
    crx_phase_clocked(c, v, 1);
    crx_clocked_step(c, vi);
}

static int crx_clocked_no_retrig(const cr_t *c, const cr_voice_t *v)
{
    return v->clk.on && !c->par[v->clk.mode][CR_P_RETRIG];
}

/* strum/slop sustain until release; harp gates; slop jitters timing only. Rates are 120-BPM reference
 * intervals scaled by the tempo (design.md 11). */
static void crx_start_performance(cr_t *c, int vi)
{
    cr_voice_t *v = &c->v[vi];
    int mode = c->perform_mode, n, i, rate, amt;
    const int16_t *p = c->par[mode];
    uint8_t ex[CR_OWN_MAX], seq[CR_SEQ_MAX];
    v->main.live = 1; v->main.n = 0;
    v->has_last = 0;
    if (mode == CR_PM_ARP || mode == CR_PM_PATTERN) { crx_start_clocked(c, vi); return; }
    n = crx_expand_range(v->notes, v->nnotes, p[CR_P_RANGE], ex);
    n = crx_order_notes(c, ex, n, p[CR_P_DIR], 0, v->notes, v->nnotes, p[CR_P_RANGE], seq);
    rate = (p[CR_P_RATE] * 120 + c->bpm / 2) / c->bpm;
    if (rate < 1) rate = 1;
    amt = mode == CR_PM_SLOP ? p[CR_P_AMOUNT] : 0;
    v->gate_t = 0;
    if (mode == CR_PM_HARP) {
        int g = (rate * p[CR_P_GATE] + 50) / 100;
        v->gate_t = (uint16_t)(g < 1 ? 1 : g);
    }
    for (i = 0; i < n; i++) {
        int due = i * rate;
        if (amt > 0) {
            int r = (rate * amt + 50) / 100;
            due += crx_randn(c, 2 * r + 1) - r;
        }
        if (due <= 0) crx_strum_fire(c, vi, seq[i]);
        else crx_sched(c, (uint32_t)due, CR_EV_STRUM, CR_OWN_V(vi), vi, seq[i]);
    }
}

/* ===================================================== streams, bass === */
/* Bass Behaviour (design.md 9.1; Choralroot interpretation, parity unverified): applies while Bass is on.
 * Solo silences main and raw; Single Notes plays main/raw only for chords; Chords Only plays bass only
 * for chords; Unison plays bass for everything at the played pitch. */
static int crx_chord_streams_ok(const cr_t *c, const cr_voice_t *v)
{
    if (!c->bass_on) return 1;
    if (c->bass_mode == CR_BASS_SOLO) return 0;
    if (c->bass_mode == CR_BASS_SINGLE_NOTES && v->rq == CR_Q_NONE) return 0;
    return 1;
}
static int crx_main_ok(const cr_t *c, const cr_voice_t *v) { return c->stream_on[CR_STREAM_MAIN] && crx_chord_streams_ok(c, v); }
static int crx_raw_ok(const cr_t *c, const cr_voice_t *v) { return c->stream_on[CR_STREAM_RAW] && crx_chord_streams_ok(c, v); }
static int crx_bass_wanted(const cr_t *c, const cr_voice_t *v)
{
    return !(c->bass_mode == CR_BASS_CHORDS_ONLY && v->rq == CR_Q_NONE);
}
static int crx_bass_ok(const cr_t *c, const cr_voice_t *v)
{
    return c->bass_on && c->stream_on[CR_STREAM_BASS] && crx_bass_wanted(c, v);
}
static int crx_bass_pitch(const cr_t *c, const cr_voice_t *v)
{
    if (c->bass_mode == CR_BASS_UNISON) return v->note_pressed + v->transpose + 12 * c->bass_voicing;
    return (CR_BASS_OCT + c->bass_voicing + 1) * 12 + v->pc + v->transpose;   /* root pc in the bass register */
}
static void crx_bass_start(cr_t *c, cr_voice_t *v)
{
    int p = crx_bass_pitch(c, v);
    v->bass.live = 1; v->bass.n = 0;
    if (crx_note_start(c, CR_STREAM_BASS, p, v->vel)) { v->bass.n = 1; v->bass.note = (uint8_t)p; }
}
static void crx_bass_release(cr_t *c, cr_voice_t *v)
{
    if (v->bass.n) crx_note_end(c, CR_STREAM_BASS, v->bass.note);
    v->bass.n = 0;
}

static void crx_main_release(cr_t *c, cr_voice_t *v)
{
    int i;
    for (i = 0; i < v->main.n; i++) crx_note_end(c, CR_STREAM_MAIN, v->main.note[i]);
    v->main.n = 0;
}
static void crx_raw_release(cr_t *c, cr_voice_t *v)
{
    int i;
    for (i = 0; i < v->raw.n; i++) crx_note_end(c, CR_STREAM_RAW, v->raw.note[i]);
    v->raw.n = 0;
}

static void crx_main_voice(cr_t *c, int vi, int on)
{
    cr_voice_t *v = &c->v[vi];
    if (!on) {
        if (v->clk.on) crx_cancel(c, CR_OWN_C(vi));
        crx_cancel(c, CR_OWN_V(vi));
        crx_main_release(c, v);
        v->main.live = 0; v->clk.on = 0;
    } else if (crx_main_ok(c, v)) {
        if (c->perform_on) crx_start_performance(c, vi);
        else {
            int i;
            v->main.live = 1; v->main.n = 0;
            for (i = 0; i < v->nnotes; i++)
                if (crx_note_start(c, CR_STREAM_MAIN, v->notes[i], v->vel)) crx_own_append(&v->main, v->notes[i]);
        }
    }
}
static void crx_raw_voice(cr_t *c, int vi, int on)
{
    cr_voice_t *v = &c->v[vi];
    if (!on) { crx_raw_release(c, v); v->raw.live = 0; }
    else if (crx_raw_ok(c, v)) {
        int i;
        v->raw.live = 1; v->raw.n = 0;
        for (i = 0; i < v->nnotes; i++)
            if (crx_note_start(c, CR_STREAM_RAW, v->notes[i], v->vel) && v->raw.n < CR_CHORD_MAX)
                v->raw.note[v->raw.n++] = v->notes[i];
    }
}
static void crx_sound_voice(cr_t *c, int vi) { crx_main_voice(c, vi, 1); crx_raw_voice(c, vi, 1); }
static void crx_hush_voice(cr_t *c, int vi) { crx_main_voice(c, vi, 0); crx_raw_voice(c, vi, 0); }

/* Update a sustained owner additively (Add Note): shared notes keep sounding. */
static void crx_sync_owner(cr_t *c, int s, uint8_t *live, uint8_t *on, uint8_t *nn, int cap,
                           const uint8_t *notes, int n, int vel)
{
    int i, j;
    if (!*live) {
        *live = 1; *nn = 0;
        for (i = 0; i < n; i++)
            if (crx_note_start(c, s, notes[i], vel) && *nn < cap) on[(*nn)++] = notes[i];
        return;
    }
    for (i = 0; i < *nn; i++) {
        int keep = 0;
        for (j = 0; j < n; j++) if (notes[j] == on[i]) keep = 1;
        if (!keep) crx_note_end(c, s, on[i]);
    }
    for (j = 0; j < n; j++) {
        int had = 0;
        for (i = 0; i < *nn; i++) if (on[i] == notes[j]) had = 1;
        if (!had) crx_note_start(c, s, notes[j], vel);
    }
    for (j = 0; j < n && j < cap; j++) on[j] = notes[j];
    *nn = (uint8_t)(n < cap ? n : cap);
}

/* ============================================================ voices === */
static void crx_disp_snapshot(cr_t *c, const cr_voice_t *v)
{
    int i;
    c->disp_valid = 1;
    c->disp_q = v->rq;
    c->disp_x = v->rq ? v->sx : 0;
    c->disp_pc = (uint8_t)crx_mod(v->root + v->transpose, 12);
    c->disp_n = v->nnotes;
    for (i = 0; i < v->nnotes; i++) c->disp_notes[i] = v->notes[i];
}

static void crx_lp_detach(cr_t *c, int vi)
{
    cr_voice_t *v = &c->v[vi];
    cr_lastplayed_t *lp = &c->lp;
    if (!lp->valid || !lp->attached || lp->vi != vi || lp->serial != v->serial) return;
    lp->key = v->key; lp->id = v->id; lp->vel = v->vel;
    lp->pc_pressed = v->pc_pressed; lp->note_pressed = v->note_pressed;
    lp->sq = v->sq; lp->sx = v->sx;
    lp->attached = 0;
}

/* a looper's capture hook (cr_out_t.gesture): keyed voices and pads, never a loop voice */
static void crx_gesture(cr_t *c, int vi, int on)
{
    const cr_voice_t *v = &c->v[vi];
    if (!c->out.gesture || !v->used || v->loop) return;
    c->out.gesture(c->out.ud, v->gid, (int16_t)(v->root + v->transpose), v->rq, v->sx, v->vel, on);
}

static void crx_kill_voice(cr_t *c, int vi)
{
    cr_voice_t *v = &c->v[vi];
    int i, best = -1;
    crx_gesture(c, vi, 0);
    crx_hush_voice(c, vi);
    crx_bass_release(c, v);
    v->bass.live = 0;
    crx_lp_detach(c, vi);
    if (c->disp_vi == vi) crx_disp_snapshot(c, v);
    v->used = 0;
    if (c->disp_vi == vi) {   /* show the most recent chord still sounding */
        for (i = 0; i < CR_NVOICE; i++)
            if (c->v[i].used && (best < 0 || (int32_t)(c->v[i].serial - c->v[best].serial) > 0)) best = i;
        c->disp_vi = (uint8_t)(best < 0 ? (int)CR_NONE : best);
    }
}

static void crx_note_last_chord(cr_t *c, int vi)
{
    const cr_voice_t *v = &c->v[vi];
    c->last_chord.used = 1;
    c->last_chord.root = v->root;
    c->last_chord.q = v->rq;
    c->last_chord.x = v->sx;
    c->last_chord.vel = v->vel;
    if (c->rec_slot >= 0) c->rec_played = 1;
    c->disp_vi = (uint8_t)vi;
}

static int crx_find_voice(const cr_t *c, int key)
{
    int i;
    for (i = 0; i < CR_MAX_VOICES; i++) if (c->v[i].used && c->v[i].key == key) return i;
    return -1;
}

/* build, register and sound a voice; bass follows it */
static int crx_start_voice(cr_t *c, int key, int id, int vel, int pc_pressed, int note_pressed, int sq, int sx, int is_free)
{
    cr_voice_t *v;
    int vi;
    for (vi = 0; vi < CR_MAX_VOICES; vi++) if (!c->v[vi].used) break;
    if (vi >= CR_MAX_VOICES) return -1;
    v = &c->v[vi];
    crx_zero(v, sizeof *v);
    v->used = 1;
    v->key = (uint8_t)key; v->id = (uint8_t)id; v->vel = (uint8_t)vel;
    v->pc_pressed = (uint8_t)pc_pressed; v->note_pressed = (uint8_t)note_pressed;
    v->transpose = c->transpose;
    v->sq = (uint8_t)sq; v->sx = (uint8_t)sx; v->is_free = (uint8_t)is_free;
    v->serial = ++c->serial;
    v->gid = ++c->gid;
    crx_build_base(c, v);
    v->nnotes = (uint8_t)cr_voice_apply(v->base, v->nbase, c->voicing, v->notes);
    crx_sound_voice(c, vi);
    if (crx_bass_ok(c, v)) crx_bass_start(c, v);
    c->disp_vi = (uint8_t)vi;
    crx_gesture(c, vi, 1);
    return vi;
}

static void crx_clear_manual_voices(cr_t *c)
{
    int i;
    for (i = 0; i < CR_MAX_VOICES; i++) if (c->v[i].used) crx_kill_voice(c, i);
}
static void crx_clear_free_voices(cr_t *c)
{
    int i;
    for (i = 0; i < CR_MAX_VOICES; i++) if (c->v[i].used && c->v[i].is_free) crx_kill_voice(c, i);
}

/* A gesture latches (sounds after release, replaced by the next press) under Sticky, or a Perform mode with
 * Hold on (arp Hold). latch_sync() flushes latched voices when the latch ends, so no note is stranded. */
int cr_latching(const cr_t *c)
{
    return c->sticky || (c->perform_on && c->par[c->perform_mode][CR_P_HOLD]);
}
static void crx_latch_sync(cr_t *c)
{
    int i;
    if (cr_latching(c)) return;
    for (i = 0; i < CR_MAX_VOICES; i++) {
        const cr_voice_t *v = &c->v[i];
        if (v->used && !(c->phys[v->key] & (1u << v->id))) crx_kill_voice(c, i);   /* latched, not held */
    }
}

/* chord pads: arm, play a chord, lift every finger -> stored (design.md 10.7) */
static void crx_rec_commit(cr_t *c)
{
    if (c->rec_slot < 0 || !c->rec_played || c->phys_n || c->mods_down) return;
    c->pads[c->rec_slot] = c->last_chord;
    c->pads[c->rec_slot].used = 1;
    c->rec_slot = -1; c->rec_played = 0;
}

static void crx_bass_sync(cr_t *c, cr_voice_t *v)
{
    int want;
    if (!c->bass_on || !c->stream_on[CR_STREAM_BASS]) return;   /* bass toggles apply to the next gesture */
    want = crx_bass_wanted(c, v);
    if (want && !v->bass.live) crx_bass_start(c, v);
    else if (!want && v->bass.live) { crx_bass_release(c, v); v->bass.live = 0; }
}

/* play styles (design.md 7). Simple: spec frozen at note-on. Advanced: modifier presses mutate held
 * voices. Free: also retriggers the last state when no root is held. */
static void crx_transform_voice(cr_t *c, int vi, int reattack)
{
    cr_voice_t *v = &c->v[vi];
    crx_build_base(c, v);
    v->nnotes = (uint8_t)cr_voice_apply(v->base, v->nbase, c->voicing, v->notes);
    crx_note_last_chord(c, vi);
    /* Full re-strike (not performing): Chord Type press (reattack) or Play Chord; else Add Note diffs. */
    if (!c->perform_on && (reattack || c->extadd == CR_EXTADD_PLAY_CHORD)) {
        crx_hush_voice(c, vi); crx_sound_voice(c, vi);
        crx_bass_sync(c, v);
        return;
    }
    if (c->stream_on[CR_STREAM_RAW]) {
        if (crx_chord_streams_ok(c, v))
            crx_sync_owner(c, CR_STREAM_RAW, &v->raw.live, v->raw.note, &v->raw.n, CR_CHORD_MAX, v->notes, v->nnotes, v->vel);
        else { crx_raw_release(c, v); v->raw.live = 0; }
    }
    if (c->perform_on) {
        if (crx_clocked_no_retrig(c, v)) crx_build_clocked_seq(c, v);   /* substitute into the running phase */
        else { crx_main_voice(c, vi, 0); crx_main_voice(c, vi, 1); }
        crx_bass_sync(c, v);
        return;
    }
    if (c->stream_on[CR_STREAM_MAIN]) {
        if (crx_chord_streams_ok(c, v))
            crx_sync_owner(c, CR_STREAM_MAIN, &v->main.live, v->main.note, &v->main.n, CR_OWN_MAX, v->notes, v->nnotes, v->vel);
        else crx_main_voice(c, vi, 0);
    }
    crx_bass_sync(c, v);
}

static void crx_lp_get(const cr_t *c, cr_lastplayed_t *o)
{
    *o = c->lp;
    if (c->lp.attached) {
        const cr_voice_t *v = &c->v[c->lp.vi];
        if (v->used && v->serial == c->lp.serial) {
            o->key = v->key; o->id = v->id; o->vel = v->vel;
            o->pc_pressed = v->pc_pressed; o->note_pressed = v->note_pressed;
            o->sq = v->sq; o->sx = v->sx;
        }
    }
}

static void crx_modifier_press(cr_t *c, int m)
{
    int isq = m <= CR_MOD_SUS, q = m + 1, xb = isq ? 0 : 1 << (m - CR_MOD_6), i, transformed = 0;
    if (c->playstyle == CR_PS_SIMPLE) return;
    for (i = 0; i < CR_MAX_VOICES; i++) {
        cr_voice_t *v = &c->v[i];
        int s;
        if (!v->used) continue;
        /* Advanced/Free: last quality wins (a Chord Type press re-strikes); an extension press TOGGLES */
        if (isq) v->sq = (uint8_t)q;
        else v->sx ^= (uint8_t)xb;
        s = crx_secret_quality(c);
        if (s) v->sq = (uint8_t)s;
        crx_transform_voice(c, i, isq);
        transformed = 1;
    }
    if (!transformed && c->playstyle == CR_PS_FREE && c->lp.valid) {
        cr_lastplayed_t lp;   /* the last real voice: its root, velocity, spec */
        int sq, sx;
        crx_lp_get(c, &lp);
        sq = lp.sq; sx = lp.sx;
        if (isq) sq = q; else sx |= xb;
        crx_start_voice(c, lp.key, lp.id, lp.vel, lp.pc_pressed, lp.note_pressed, sq, sx, 1);
    }
}

static void crx_modifier_release(cr_t *c)
{
    crx_rec_commit(c);
    if (c->mods_down) return;   /* Free voices need a held modifier */
    crx_clear_free_voices(c);
}

/* ================================================================ API === */
void cr_init(cr_t *c, const cr_out_t *out)
{
    int m, p;
    crx_zero(c, sizeof *c);
    if (out) c->out = *out;
    c->playstyle = CR_PS_SIMPLE;
    c->extadd = CR_EXTADD_ADD_NOTE;
    c->secret = CR_SECRET_OFF;
    c->scale = CR_SCALE_MAJOR;
    c->bass_mode = CR_BASS_CHORDS_ONLY;
    c->perform_mode = CR_PM_STRUM;
    c->bpm = 120;
    c->single = CR_SINGLE_FULL;
    c->split_pc = 5;
    c->stream_on[CR_STREAM_MAIN] = 1;
    c->stream_on[CR_STREAM_BASS] = 1;
    c->stream_on[CR_STREAM_RAW] = 0;
    for (m = 0; m < CR_PM_COUNT; m++)
        for (p = 0; p < CR_P_COUNT; p++) c->par[m][p] = CR_PAR_DEFAULT[m][p];
    c->rng = 0x2545F491u;
    c->rec_slot = -1;
    c->disp_vi = CR_NONE;
}

void cr_seed(cr_t *c, uint32_t seed) { c->rng = seed ? seed : 0x2545F491u; }

void cr_clock_reset(cr_t *c)
{
    c->started = 1;
    c->clk_anchor = c->now; c->clk_rem = 0; c->clk_units = 0;
}
uint32_t cr_clock_epoch(const cr_t *c) { return c->clk_anchor; }

void cr_tick(cr_t *c, uint32_t now_ms)
{
    int d = 0, k;
    uint32_t block = 2u * (960000u / c->bpm);
    c->now = now_ms;
    if (!c->started) cr_clock_reset(c);
    /* keep the global grid anchor recent: advance it by whole 16-beat blocks (every division's grid) */
    while ((int32_t)(c->now - c->clk_anchor) > 0 && c->now - c->clk_anchor > block) {
        uint32_t total = 960000u + c->clk_rem;
        c->clk_anchor += total / c->bpm;
        c->clk_rem = (uint16_t)(total % c->bpm);
        c->clk_units += CR_BLOCK_UNITS;
    }
    /* drain the due prefix in order; handlers only append later events behind it */
    while (d < c->nev && (int32_t)(c->ev[d].due - c->now) <= 0) d++;
    if (!d) return;
    for (k = 0; k < d; k++) {
        cr_ev_t e = c->ev[k];
        if (e.kind == CR_EV_NOTE_OFF) crx_perf_note_off(c, e.vi, e.note);
        else if (e.kind == CR_EV_STRUM) crx_strum_fire(c, e.vi, e.note);
        else if (e.kind == CR_EV_CLOCK) {
            cr_voice_t *v = &c->v[e.vi];
            if (v->used && v->clk.on) { crx_fire_clocked_once(c, e.vi); crx_clocked_step(c, e.vi); }
        }
    }
    for (k = d; k < c->nev; k++) c->ev[k - d] = c->ev[k];
    c->nev = (uint16_t)(c->nev - d);
}

void cr_key_ex(cr_t *c, uint8_t note, uint8_t id, uint8_t vel, int down)
{
    uint8_t bit;
    int vi;
    if (note > 127) return;
    id &= 7u;
    bit = (uint8_t)(1u << id);
    if (down) {
        cr_voice_t *v;
        /* Once every key of a latched gesture has been lifted, the first press of the next gesture replaces
         * the latched group instead of adding to it. */
        if (cr_latching(c) && c->phys_n == 0) crx_clear_manual_voices(c);
        if (!(c->phys[note] & bit)) { c->phys[note] |= bit; c->phys_n++; }
        vi = crx_find_voice(c, note);
        if (vi >= 0 && c->v[vi].is_free) { crx_kill_voice(c, vi); vi = -1; }   /* a real press replaces a Free voice */
        if (vi >= 0) {
            /* same root: release, retrigger the same chord at the new velocity (design.md 5.3) */
            v = &c->v[vi];
            crx_gesture(c, vi, 0);   /* a looper: one gesture ends, the retrigger begins another */
            v->gid = ++c->gid;
            crx_bass_release(c, v);
            v->superseded |= (uint8_t)(1u << v->id);
            v->id = id;
            v->vel = vel;
            if (!crx_clocked_no_retrig(c, v)) { crx_hush_voice(c, vi); crx_sound_voice(c, vi); }   /* retrig off: continues */
            if (v->bass.live) crx_bass_start(c, v);
            crx_gesture(c, vi, 1);
        } else {
            uint8_t sq, sx;
            crx_snapshot_spec(c, &sq, &sx);   /* spec captured at note-on */
            vi = crx_start_voice(c, note, id, vel, note % 12, note, sq, sx, 0);
            if (vi < 0) return;
            c->lp.valid = 1; c->lp.attached = 1; c->lp.vi = (uint8_t)vi; c->lp.serial = c->v[vi].serial;
        }
        crx_note_last_chord(c, vi);
    } else {
        if (c->phys[note] & bit) { c->phys[note] &= (uint8_t)~bit; c->phys_n--; }
        crx_rec_commit(c);
        if (cr_latching(c)) return;
        vi = crx_find_voice(c, note);
        if (vi < 0) return;
        if (c->v[vi].id == id) crx_kill_voice(c, vi);
        else if (c->v[vi].superseded & bit) c->v[vi].superseded &= (uint8_t)~bit;   /* superseded press: ignored */
    }
}

void cr_key(cr_t *c, uint8_t midi_note, uint8_t vel, int down) { cr_key_ex(c, midi_note, 0, vel, down); }

void cr_mod(cr_t *c, cr_mod_t m, int down)
{
    int mi = (int)m, i;
    uint8_t bit;
    if (mi < 0 || mi >= CR_NMOD) return;
    bit = (uint8_t)(1u << mi);
    if (down) {
        if (c->mods_down & bit) return;
        c->mods_down |= bit;
        if (mi <= CR_MOD_SUS && c->qn < CR_NMOD) c->qstack[c->qn++] = (uint8_t)(mi + 1);
        crx_modifier_press(c, mi);
    } else {
        if (!(c->mods_down & bit)) return;
        c->mods_down &= (uint8_t)~bit;
        if (mi <= CR_MOD_SUS)
            for (i = (int)c->qn - 1; i >= 0; i--)
                if (c->qstack[i] == mi + 1) {
                    for (; i + 1 < c->qn; i++) c->qstack[i] = c->qstack[i + 1];
                    c->qn--;
                    break;
                }
        crx_modifier_release(c);
    }
}

/* live re-voice of sounding chords (design.md 8/17) */
void cr_voicing_step(cr_t *c, int d)
{
    int nv = crx_clamp(c->voicing + d, CR_VOICE_MIN, CR_VOICE_MAX), i;
    if (nv == c->voicing) return;
    c->voicing = (int8_t)nv;
    for (i = 0; i < CR_NVOICE; i++) {
        cr_voice_t *v = &c->v[i];
        if (!v->used) continue;
        crx_hush_voice(c, i);
        v->nnotes = (uint8_t)cr_voice_apply(v->base, v->nbase, nv, v->notes);
        crx_sound_voice(c, i);
    }
}

void cr_bass_voicing_step(cr_t *c, int d)
{
    int nv = crx_clamp(c->bass_voicing + d, CR_BV_MIN, CR_BV_MAX), i;
    if (nv == c->bass_voicing) return;
    c->bass_voicing = (int8_t)nv;
    for (i = 0; i < CR_NVOICE; i++) {
        cr_voice_t *v = &c->v[i];
        if (v->used && v->bass.live) { crx_bass_release(c, v); crx_bass_start(c, v); }
    }
}

/* panic, note/scheduler half (design.md 15.2): offs for every tracked note, CC 123 on the streams in use */
void cr_panic(cr_t *c)
{
    int s, n;
    c->nev = 0;
    for (s = 0; s < CR_NSTREAM; s++)
        for (n = 0; n < 128; n++)
            if (c->reg[s][n]) { crx_out_off(c, s, n); c->reg[s][n] = 0; }
    if (c->out.all_off) {
        c->out.all_off(c->out.ud, CR_STREAM_MAIN);   /* all three, always: the caller decides what */
        c->out.all_off(c->out.ud, CR_STREAM_BASS);   /* a disabled stream does with it */
        c->out.all_off(c->out.ud, CR_STREAM_RAW);
    }
    for (n = 0; n < CR_NVOICE; n++) c->v[n].used = 0;
    c->rec_slot = -1; c->rec_played = 0;
    c->lp.valid = 0;
    c->disp_vi = CR_NONE; c->disp_valid = 0;
}

/* ---------------------------------------------------------- settings --- */
static void crx_restart_sounding_voices(cr_t *c)
{
    int i;
    for (i = 0; i < CR_NVOICE; i++)
        if (c->v[i].used) { crx_main_voice(c, i, 0); crx_main_voice(c, i, 1); }
}

void cr_set_playstyle(cr_t *c, cr_playstyle_t p)
{
    if ((int)p < CR_PS_SIMPLE || (int)p > CR_PS_FREE || c->playstyle == (uint8_t)p) return;
    c->playstyle = (uint8_t)p;
    c->lp.valid = 0;   /* new semantics invalidate Free state (design.md 7/17) */
    crx_clear_free_voices(c);
}
void cr_set_ext_addition(cr_t *c, cr_extadd_t e) { c->extadd = (uint8_t)(e == CR_EXTADD_PLAY_CHORD); }
void cr_set_secret(cr_t *c, cr_secret_t s) { if ((int)s >= CR_SECRET_OFF && (int)s <= CR_SECRET_ALL) c->secret = (uint8_t)s; }
void cr_set_key(cr_t *c, int enabled, uint8_t tonic_pc, cr_scale_t scale)
{
    c->key_on = (uint8_t)(enabled != 0);
    c->tonic = (uint8_t)(tonic_pc % 12u);
    c->scale = (uint8_t)(scale == CR_SCALE_MINOR);
}
void cr_set_transpose(cr_t *c, int semis) { c->transpose = (int8_t)crx_clamp(semis, -24, 24); }
void cr_set_single_notes(cr_t *c, cr_single_t mode, int split_pc)
{
    c->single = (uint8_t)(mode == CR_SINGLE_SPLIT);
    c->split_pc = (uint8_t)crx_clamp(split_pc, 0, 11);
}
void cr_set_bass(cr_t *c, int enabled) { c->bass_on = (uint8_t)(enabled != 0); }
void cr_set_bass_mode(cr_t *c, cr_bassmode_t m) { if ((int)m >= CR_BASS_CHORDS_ONLY && (int)m <= CR_BASS_SOLO) c->bass_mode = (uint8_t)m; }
void cr_set_stream(cr_t *c, cr_stream_t s, int enabled) { if ((int)s >= 0 && (int)s < CR_NSTREAM) c->stream_on[s] = (uint8_t)(enabled != 0); }

void cr_set_perform(cr_t *c, int enabled)
{
    enabled = enabled != 0;
    if (c->perform_on == enabled) return;
    c->perform_on = (uint8_t)enabled;
    crx_restart_sounding_voices(c);   /* design.md 11.1/17 */
    crx_latch_sync(c);                /* disabling Perform releases any Hold-latched voices */
}

void cr_set_perform_mode(cr_t *c, cr_pmode_t m)
{
    if ((int)m < 0 || (int)m >= CR_PM_COUNT || c->perform_mode == (uint8_t)m) return;
    c->perform_mode = (uint8_t)m;
    if (c->perform_on) crx_restart_sounding_voices(c);
    crx_latch_sync(c);   /* leaving a Hold mode releases latched voices */
}

/* Timing rephases in place; finite modes refresh from the held chord (design.md 17). */
void cr_set_param(cr_t *c, cr_pmode_t m, cr_param_t p, int value)
{
    int i;
    if ((int)m < 0 || (int)m >= CR_PM_COUNT || (int)p < 0 || (int)p >= CR_P_COUNT) return;
    value = crx_clamp(value, CR_PAR_MIN[p], CR_PAR_MAX[p]);
    if (c->par[m][p] == value) return;
    c->par[m][p] = (int16_t)value;
    for (i = 0; i < CR_NVOICE; i++) {
        cr_voice_t *v = &c->v[i];
        if (!v->used) continue;
        if (v->clk.on) {
            if (v->clk.mode != m) continue;
            if (p == CR_P_DIR || p == CR_P_RANGE) crx_build_clocked_seq(c, v);
            else if (p == CR_P_DIV || p == CR_P_SWING || p == CR_P_RETRIG) crx_rephase_clocked(c, i);
        } else if (c->perform_on && m == c->perform_mode) {
            crx_main_voice(c, i, 0); crx_main_voice(c, i, 1);
        }
    }
    if (p == CR_P_HOLD) crx_latch_sync(c);   /* Hold turned off: release latched voices */
}
int cr_get_param(const cr_t *c, cr_pmode_t m, cr_param_t p)
{
    if ((int)m < 0 || (int)m >= CR_PM_COUNT || (int)p < 0 || (int)p >= CR_P_COUNT) return 0;
    return c->par[m][p];
}

void cr_set_sticky(cr_t *c, int on)
{
    on = on != 0;
    if (c->sticky == on) return;
    c->sticky = (uint8_t)on;
    crx_latch_sync(c);
}

/* tempo (design.md 20): the grid restarts and clocked voices rephase keeping their melodic position.
 * Deliberate deviation from 20: a finite Strum/Slop/Harp gesture is NOT restarted (BPM is an encoder; every
 * detent would re-strum). It keeps its scheduled events; the new rate applies to the next gesture. */
void cr_set_tempo(cr_t *c, int bpm)
{
    int i;
    bpm = crx_clamp(bpm, 20, 300);
    if (bpm == c->bpm) return;
    c->bpm = (uint16_t)bpm;
    cr_clock_reset(c);
    for (i = 0; i < CR_NVOICE; i++)
        if (c->v[i].used && c->v[i].clk.on) crx_rephase_clocked(c, i);
}

/* --------------------------------------------------------- chord pads --- */
void cr_pad_arm(cr_t *c, int slot)
{
    if (slot < 0 || slot >= CR_MAX_PADS) return;
    c->rec_slot = (int8_t)slot; c->rec_played = 0;
}
int cr_pad_armed(const cr_t *c) { return c->rec_slot; }

void cr_pad_up(cr_t *c, int slot)
{
    int vi = CR_MAX_VOICES + slot;
    if (slot < 0 || slot >= CR_MAX_PADS || !c->v[vi].used) return;
    crx_kill_voice(c, vi);
}

void cr_pad_down(cr_t *c, int slot)
{
    const cr_chord_t *d;
    cr_voice_t *v;
    int vi = CR_MAX_VOICES + slot;
    if (slot < 0 || slot >= CR_MAX_PADS || !c->pads[slot].used) return;
    cr_pad_up(c, slot);
    d = &c->pads[slot];
    v = &c->v[vi];
    crx_zero(v, sizeof *v);
    v->used = 1; v->pad = (uint8_t)(slot + 1);
    v->vel = d->vel;
    v->note_pressed = (uint8_t)crx_clamp(d->root, 0, 127);
    v->pc = v->pc_pressed = (uint8_t)crx_mod(d->root, 12);
    v->transpose = c->transpose;
    v->sq = v->rq = d->q; v->sx = d->x;
    v->root = d->root;
    v->serial = ++c->serial;
    v->gid = ++c->gid;
    v->nbase = (uint8_t)cr_chord_base(d->root + v->transpose, d->q, d->x, v->base);
    v->nnotes = (uint8_t)cr_voice_apply(v->base, v->nbase, c->voicing, v->notes);
    crx_sound_voice(c, vi);
    if (crx_bass_ok(c, v)) crx_bass_start(c, v);
    c->disp_vi = (uint8_t)vi;
    crx_gesture(c, vi, 1);
}

void cr_pad_stop_all(cr_t *c)
{
    int i;
    for (i = 0; i < CR_MAX_PADS; i++) cr_pad_up(c, i);
}

int cr_pad_get(const cr_t *c, int slot, int16_t *root, uint8_t *q, uint8_t *ext, uint8_t *vel)
{
    const cr_chord_t *d;
    if (slot < 0 || slot >= CR_MAX_PADS || !c->pads[slot].used) return 0;
    d = &c->pads[slot];
    if (root) *root = d->root;
    if (q) *q = d->q;
    if (ext) *ext = d->x;
    if (vel) *vel = d->vel;
    return 1;
}

void cr_pad_set(cr_t *c, int slot, int used, int16_t root, uint8_t q, uint8_t ext, uint8_t vel)
{
    cr_chord_t *d;
    if (slot < 0 || slot >= CR_MAX_PADS) return;
    d = &c->pads[slot];
    d->used = (uint8_t)(used != 0);
    d->root = root;
    d->q = q < CR_Q_COUNT ? q : 0;
    d->x = (uint8_t)(ext & 15u);
    d->vel = (uint8_t)crx_clamp(vel, 1, 127);
}

/* -------------------------------------------------------- loop voices --- */
int cr_loop_busy(const cr_t *c, int lv)
{
    return lv >= 0 && lv < CR_MAX_LOOPV && c->v[CR_MAX_VOICES + CR_MAX_PADS + lv].used;
}

int cr_loop_event(cr_t *c, int lv, int16_t root, uint8_t q, uint8_t ext, uint8_t vel, int on)
{
    int vi = CR_MAX_VOICES + CR_MAX_PADS + lv;
    cr_voice_t *v;
    if (lv < 0 || lv >= CR_MAX_LOOPV) return 0;
    if (c->v[vi].used) crx_kill_voice(c, vi);
    if (!on) return 0;
    if (q >= CR_Q_COUNT) q = CR_Q_NONE;
    root = (int16_t)crx_clamp(root, 0, 127);
    v = &c->v[vi];
    crx_zero(v, sizeof *v);
    v->used = 1; v->loop = (uint8_t)(lv + 1);
    v->vel = (uint8_t)crx_clamp(vel, 1, 127);
    v->note_pressed = (uint8_t)root;
    v->pc = v->pc_pressed = (uint8_t)crx_mod(root, 12);
    v->transpose = 0;                /* the recorded root is absolute */
    v->sq = v->rq = q; v->sx = (uint8_t)(ext & 15u);
    v->root = root;
    v->serial = ++c->serial;
    v->nbase = (uint8_t)cr_chord_base(root, q, v->sx, v->base);
    v->nnotes = (uint8_t)cr_voice_apply(v->base, v->nbase, c->voicing, v->notes);
    crx_sound_voice(c, vi);
    if (crx_bass_ok(c, v)) crx_bass_start(c, v);
    c->disp_vi = (uint8_t)vi;
    return 1;
}

/* ----------------------------------------------------------- queries --- */
static void crx_str(char *d, const char *s, int max)
{
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = 0;
}
static void crx_cat(char *d, const char *s, int max)
{
    int i = 0;
    while (d[i]) i++;
    if (i && *s && i < max - 1) d[i++] = ' ';
    while (*s && i < max - 1) d[i++] = *s++;
    d[i] = 0;
}

/* Orchid Standard Chord Naming (manual 6.2-6.5): triads at root size (C Cm Csus Cdim C+); the power chord
 * and Min+Sus secret print as superscript ("C" "5", "Cm" "add4", manual's secret-chord table), followed by
 * the extensions: 7ths first then 6 then 9 ("M7 9", "7 6"); a minor 7th prints "7" (the dominant anomaly,
 * also Cm7 / Cdim7) except on sus ("m7"); 3 extensions = JAZZ, 4 = WTF. */
void cr_chord_name(uint8_t root_pc, uint8_t quality, uint8_t ext, char root[3], char qual[5], char sup[CR_SUP_MAX])
{
    crx_str(root, CR_NOTE_NAMES[root_pc % 12u], 3);
    sup[0] = 0;
    if (quality >= CR_Q_COUNT) quality = CR_Q_NONE;
    crx_str(qual, CR_QUAL_NAMES[quality], 5);
    if (quality == CR_Q_NONE) return;
    if (quality == CR_Q_POW) crx_str(sup, "5", CR_SUP_MAX);
    else if (quality == CR_Q_MIN4) crx_str(sup, "add4", CR_SUP_MAX);
    ext &= 15u;
    if (crx_bits(ext) >= 4) { crx_cat(sup, "WTF", CR_SUP_MAX); return; }
    if (crx_bits(ext) == 3) { crx_cat(sup, "JAZZ", CR_SUP_MAX); return; }
    if (ext & CR_EXT_M7) crx_cat(sup, quality == CR_Q_SUS ? "m7" : "7", CR_SUP_MAX);
    if (ext & CR_EXT_MAJ7) crx_cat(sup, "M7", CR_SUP_MAX);
    if (ext & CR_EXT_6) crx_cat(sup, "6", CR_SUP_MAX);
    if (ext & CR_EXT_9) crx_cat(sup, "9", CR_SUP_MAX);
}

void cr_note_name(uint8_t note, char buf[5])
{
    const char *n = CR_NOTE_NAMES[note % 12u];
    int o = note / 12 - 1, i = 0;
    while (*n) buf[i++] = *n++;
    if (o < 0) { buf[i++] = '-'; o = -o; }
    buf[i++] = (char)('0' + o);
    buf[i] = 0;
}

void cr_chord_info(const cr_t *c, cr_chord_info_t *ci)
{
    int i;
    crx_zero(ci, sizeof *ci);
    if (c->disp_vi != CR_NONE && c->v[c->disp_vi].used) {
        const cr_voice_t *v = &c->v[c->disp_vi];
        ci->valid = 1; ci->sounding = 1;
        ci->root_pc = (uint8_t)crx_mod(v->root + v->transpose, 12);
        ci->quality = v->rq;
        ci->ext = v->rq ? v->sx : 0;
        ci->nnotes = v->nnotes;
        for (i = 0; i < v->nnotes; i++) ci->notes[i] = v->notes[i];
    } else if (c->disp_valid) {
        ci->valid = 1;
        ci->root_pc = c->disp_pc; ci->quality = c->disp_q; ci->ext = c->disp_x;
        ci->nnotes = c->disp_n;
        for (i = 0; i < c->disp_n; i++) ci->notes[i] = c->disp_notes[i];
    } else {
        ci->root[0] = ci->qual[0] = ci->sup[0] = 0;
        return;
    }
    ci->secret = ci->quality >= CR_Q_AUG;
    cr_chord_name(ci->root_pc, ci->quality, ci->ext, ci->root, ci->qual, ci->sup);
}

uint16_t cr_scale_mask(const cr_t *c)
{
    uint16_t m = 0;
    int r;
    if (!c->key_on) return 0;
    for (r = 0; r < 12; r++)
        if (CR_KEYMAP[c->scale][r] < 8u) m |= (uint16_t)(1u << ((c->tonic + r) % 12));
    return m;
}

uint8_t cr_mods_held(const cr_t *c) { return c->mods_down; }

/* modifier LEDs: physically held, or (Advanced/Free) in a held voice's spec, so toggled extensions and the
 * active quality stay lit after release */
uint8_t cr_mods_active(const cr_t *c)
{
    uint8_t m = c->mods_down;
    int i;
    if (c->playstyle == CR_PS_SIMPLE) return m;
    for (i = 0; i < CR_MAX_VOICES; i++) {
        const cr_voice_t *v = &c->v[i];
        if (!v->used) continue;
        if (v->sq >= CR_Q_DIM && v->sq <= CR_Q_SUS) m |= (uint8_t)(1u << (v->sq - 1));
        m |= (uint8_t)((v->sx & 15u) << 4);
    }
    return m;
}

int cr_perform_pos(const cr_t *c, uint8_t *note)
{
    const cr_voice_t *v;
    int i;
    if (c->disp_vi == CR_NONE || !c->perform_on) return -1;
    v = &c->v[c->disp_vi];
    if (!v->used || !v->main.live || !v->has_last) return -1;
    if (note) *note = v->last_note;
    for (i = 0; i < v->nnotes; i++) if (v->notes[i] == v->last_note) return i;
    for (i = 0; i < v->nnotes; i++)
        if (v->last_note >= v->notes[i] && (v->last_note - v->notes[i]) % 12 == 0) return i;
    return -1;
}

int cr_sounding(const cr_t *c, cr_stream_t s, uint8_t note)
{
    if ((int)s < 0 || (int)s >= CR_NSTREAM || note > 127) return 0;
    return c->reg[s][note];
}
int cr_voices(const cr_t *c)
{
    int i, n = 0;
    for (i = 0; i < CR_NVOICE; i++) n += c->v[i].used;
    return n;
}
int cr_pending(const cr_t *c) { return c->nev; }
const char *cr_pattern_name(int pattern)
{
    return pattern >= 1 && pattern <= CR_NPATTERN ? CR_PATTERN_NAMES[pattern - 1] : "";
}
