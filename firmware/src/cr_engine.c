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
static const uint32_t CR_QUALITY[CR_Q_COUNT] = { 0, 0x6303, 0x7303, 0x7403, 0x7503, 0x8403, 0xC703, 0x5303,
    /* the 6-Dim scales' (0.36): 6/3 = 0 3 5 8, 6/5 = 0 2 5 9, m6/3 = 0 4 6 9, m6/5 = 0 2 5 8, 7b9 = 0 1 4 7 10 */
    0x85304, 0x95204, 0x96404, 0x85204, 0xA74105 };

/* Extension intervals in mask-bit order 6, m7, M7, 9 (ORCHID_FIRMWARE_REFERENCE.md 6.2, high-confidence). */
static const uint8_t CR_EXT_IV[4] = { 9, 10, 11, 14 };
#define CR_SIXTH_UP_IV 21        /* the 6th an octave up: the 13th (cr_chord_base_ex) */

/* Key Mode rule table (design.md 10.3/10.4): per scale (cr_scale_t), one entry per pitch class relative to the tonic.
 * Bits 0-3 = quality (CR_Q_*), bits 4-5 = root shift (0 none, 1 down, 2 up), bits 8-11 = extensions the scale adds
 * (CR_EXT_*: the 6th of C6 and of the diminished seventh, the 7th of C7 / C+7). Diatonic degrees keep the triad stacked
 * in the scale; chromatic degrees resolve to a neighbour as SUS (C# in C -> Csus, measured; the rest a labelled
 * fallback: lower neighbour, upward where its sus4 leaves the scale). Major and Minor are the tables of 0.35, the
 * others added in 0.36 (GitHub issues 34, 45; docs/MANUAL.md "Key Mode" has them spelt out):
 * - the modes, Harm / Mel Minor, Phryg Dom: the same rules. Dbl Harm's G and B stack no triad: G and Bdim
 * - Maj Pent / Min Pent / Blues / Hirajoshi: the lights, Notes and Snap take their 5 / 6 notes (CR_SCALE_MASK); the
 *   chords are Major's / Minor's (Blues' b5: dim)
 * - Bebop Dom / Maj / Min: Mixolydian / Major / Dorian with the passing note a diminished seventh
 * - Whole Tone: C+7 on its notes, the augmented triad on the others. Dim W-H: dim7 on its notes; Dim H-W: C7 on
 *   C Eb F# A, dim7 on the others; out of either: X7b9
 * - 6-Dim (Barry Harris): C6 on C, its inversions over E G A (C6/E, C6/G, Am7 = C6/A), dim7 on D F G# B, X7b9 out of
 *   the scale; m6-Dim the same with Cm6 (A: Am7b5)
 * Generated (and Major / Minor checked against 0.35's) by tools/gen_scales.py */
static const uint16_t CR_SCALE_MASK[CR_SCALE_COUNT] = {
    0xAB5, 0x5AD, 0x6AD, 0x5AB, 0xAD5, 0x6B5, 0x56B, 0x9AD, 0xAAD, 0x295, 0x4A9, 0x4E9, 0x5B3, 0x9B3, 0x18D, 0x555,
    0xB6D, 0x6DB, 0xEB5, 0xBB5, 0x6BD, 0xBB5, 0xBAD };
static const uint16_t CR_KEYMAP[CR_SCALE_COUNT][12] = {
    { 0x003, 0x014, 0x002, 0x014, 0x002, 0x003, 0x024, 0x003, 0x014, 0x002, 0x014, 0x001 },   /* Major */
    { 0x002, 0x014, 0x001, 0x003, 0x014, 0x002, 0x014, 0x002, 0x003, 0x024, 0x003, 0x014 },   /* Minor */
    { 0x002, 0x014, 0x002, 0x003, 0x024, 0x003, 0x014, 0x002, 0x014, 0x001, 0x003, 0x014 },   /* Dorian */
    { 0x002, 0x003, 0x024, 0x003, 0x014, 0x002, 0x014, 0x001, 0x003, 0x014, 0x002, 0x014 },   /* Phrygian */
    { 0x003, 0x024, 0x003, 0x014, 0x002, 0x014, 0x001, 0x003, 0x014, 0x002, 0x014, 0x002 },   /* Lydian */
    { 0x003, 0x014, 0x002, 0x014, 0x001, 0x003, 0x014, 0x002, 0x014, 0x002, 0x003, 0x024 },   /* Mixolydian */
    { 0x001, 0x003, 0x014, 0x002, 0x014, 0x002, 0x003, 0x024, 0x003, 0x014, 0x002, 0x014 },   /* Locrian */
    { 0x002, 0x014, 0x001, 0x005, 0x014, 0x002, 0x024, 0x003, 0x003, 0x014, 0x024, 0x001 },   /* Harm Minor */
    { 0x002, 0x014, 0x002, 0x005, 0x014, 0x003, 0x024, 0x003, 0x014, 0x001, 0x014, 0x001 },   /* Mel Minor */
    { 0x003, 0x014, 0x002, 0x014, 0x002, 0x003, 0x024, 0x003, 0x014, 0x002, 0x014, 0x001 },   /* Maj Pent */
    { 0x002, 0x014, 0x001, 0x003, 0x014, 0x002, 0x014, 0x002, 0x003, 0x024, 0x003, 0x014 },   /* Min Pent */
    { 0x002, 0x014, 0x001, 0x003, 0x014, 0x002, 0x001, 0x002, 0x003, 0x024, 0x003, 0x014 },   /* Blues */
    { 0x003, 0x003, 0x014, 0x024, 0x001, 0x002, 0x014, 0x001, 0x005, 0x014, 0x002, 0x024 },   /* Phryg Dom */
    { 0x003, 0x003, 0x014, 0x024, 0x002, 0x002, 0x014, 0x003, 0x005, 0x014, 0x024, 0x001 },   /* Dbl Harm */
    { 0x002, 0x014, 0x001, 0x003, 0x014, 0x002, 0x014, 0x002, 0x003, 0x024, 0x003, 0x014 },   /* Hirajoshi */
    { 0x205, 0x005, 0x205, 0x005, 0x205, 0x005, 0x205, 0x005, 0x205, 0x005, 0x205, 0x005 },   /* Whole Tone */
    { 0x101, 0x00C, 0x101, 0x101, 0x00C, 0x101, 0x101, 0x00C, 0x101, 0x101, 0x00C, 0x101 },   /* Dim W-H */
    { 0x203, 0x101, 0x00C, 0x203, 0x101, 0x00C, 0x203, 0x101, 0x00C, 0x203, 0x101, 0x00C },   /* Dim H-W */
    { 0x003, 0x014, 0x002, 0x014, 0x001, 0x003, 0x014, 0x002, 0x014, 0x002, 0x003, 0x101 },   /* Bebop Dom */
    { 0x003, 0x014, 0x002, 0x014, 0x002, 0x003, 0x024, 0x003, 0x101, 0x002, 0x014, 0x001 },   /* Bebop Maj */
    { 0x002, 0x014, 0x002, 0x003, 0x101, 0x003, 0x014, 0x002, 0x014, 0x001, 0x003, 0x014 },   /* Bebop Min */
    { 0x103, 0x00C, 0x101, 0x00C, 0x008, 0x101, 0x00C, 0x009, 0x101, 0x202, 0x00C, 0x101 },   /* 6-Dim */
    { 0x102, 0x00C, 0x101, 0x00A, 0x00C, 0x101, 0x00C, 0x00B, 0x101, 0x201, 0x00C, 0x101 },   /* m6-Dim */
};
#define CR_SCALE_MINORS 0x514DCEu   /* bit s: scale s is a minor one (the MIN key's; Minor's numerals) */
static const char *const CR_SCALE_NAMES[CR_SCALE_COUNT] = {
    "Major", "Minor", "Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian", "Harm Minor", "Mel Minor", "Maj Pent",
    "Min Pent", "Blues", "Phryg Dom", "Dbl Harm", "Hirajoshi", "Whole Tone", "Dim W-H", "Dim H-W", "Bebop Dom",
    "Bebop Maj", "Bebop Min", "6-Dim", "m6-Dim" };

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
static const char *const CR_QUAL_NAMES[CR_Q_COUNT] = { "", "dim", "m", "", "sus", "+", "", "m", "", "", "m", "m", "" };   /* pow / min4: sup "5" / "add4" */

#define CR_VOICE_MIN (-12)       /* chord voicing rotation clamp (design.md 8) */
#define CR_VOICE_MAX 12
#define CR_BV_MIN (-2)           /* bass register, octaves from C2 (design.md 9) */
#define CR_BV_MAX 4
#define CR_BASS_OCT 2
#define CR_BLOCK_UNITS 384u      /* 16 beats in 1/24-beat units: a whole number of every division's pair */
#define CR_NONE 0xFFu

enum { CR_EV_NOTE_OFF = 1, CR_EV_STRUM, CR_EV_CLOCK };
#define CR_OWN_V(vi, part) ((uint8_t)((unsigned)(vi) * 4u + (unsigned)(part) * 2u))        /* a voice's part's strum /
                                                                                             * note-off events */
#define CR_OWN_C(vi, part) ((uint8_t)((unsigned)(vi) * 4u + (unsigned)(part) * 2u + 1u))   /* its clocked-step events */
#define CR_OWN_PART(o) (((unsigned)(o) >> 1) & 1u)
typedef char crx_owner_fits[CR_NVOICE * 4 <= 256 ? 1 : -1];   /* the scheduler's owners fit a byte (C99) */

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
/* sixth_up (Options > 6th with 7th: Octave up; docs/CHORD-NAMES.md "The 6th above a seventh"): a chord with the 6 AND
 * a seventh (m7, M7 or both) has its 6th an octave higher, 21 semitones above the root instead of 9: the 13th, above
 * the 9th's 14. Every chord type alike (the secret ones and DIM, where it is the 13th of a half-diminished chord);
 * without a seventh (C6, C6/9) and for a single note nothing moves. It is done here, before the voicing, so KNOB 1
 * rotates it as the top note it is. Where root + 21 would leave 0..127 the 6th stays in place (the 9th up there is
 * dropped by the range rule below; a 6th that has a place inside the range keeps it): the same number of notes as
 * Off, always */
int cr_chord_base_ex(int root_note, uint8_t quality, uint8_t ext, int sixth_up, uint8_t out[CR_CHORD_MAX])
{
    uint8_t iv[CR_CHORD_MAX];
    int ni = 0, n = 0, j, k;
    if (quality > CR_Q_NONE && quality < CR_Q_COUNT) {
        uint32_t q = CR_QUALITY[quality];
        for (j = 1; j <= (int)(q & 15u); j++) iv[ni++] = (uint8_t)((q >> (j * 4)) & 15u);
        for (k = 0; k < 4; k++) {   /* extensions need a Chord Type (or Key Mode) */
            int dup = 0;
            if (!(ext & (1u << k))) continue;
            for (j = 0; j < ni; j++) if (iv[j] == CR_EXT_IV[k]) dup = 1;
            if (!dup && ni < CR_CHORD_MAX) {
                iv[ni] = CR_EXT_IV[k];
                if (sixth_up && k == 0 && (ext & (CR_EXT_M7 | CR_EXT_MAJ7)) && root_note + CR_EXT_IV[0] >= 0 &&
                    root_note + CR_SIXTH_UP_IV <= 127)
                    iv[ni] = CR_SIXTH_UP_IV;         /* (a 6th the range drops in place is not brought back either) */
                ni++;
            }
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
int cr_chord_base(int root_note, uint8_t quality, uint8_t ext, uint8_t out[CR_CHORD_MAX])
{
    return cr_chord_base_ex(root_note, quality, ext, 0, out);
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
    if (c->key_on && !c->key_notes && !(v->zone & CR_Z_BASS) && !((v->zone & CR_Z_FULL) && c->full_chords == 2)) {   /* (Key plays Notes, a bass-side key: no chord. The
                                                    * Full keyboard, Key plays Chords: the key's chord, as a lone root's
                                                    * on the Chords keyboard; until 0.22 every key there was a note) */
        int e = CR_KEYMAP[c->scale % CR_SCALE_COUNT][crx_mod(v->pc_pressed - c->tonic, 12)], sh = (e >> 4) & 3;
        if (sh == 1) { root--; pc = crx_mod(pc - 1, 12); }
        else if (sh == 2) { root++; pc = crx_mod(pc + 1, 12); }
        if (!q) {
            q = e & 15;
            v->sx |= (uint8_t)(e >> 8);   /* the scale's 6th / 7th (0.36): the voice's own from here (a loop records
                                           * it, the name shows it, a rebuild finds it set) */
        }
    }
    if (!q && root >= 12 && ((v->zone & CR_Z_LOW) ||
                             (!v->zone && c->single == CR_SINGLE_SPLIT && v->pc_pressed < c->split_pc)))
        root -= 12;                  /* Single Notes Split Oct: below the split point, an octave lower (the Chords
                                      * layout: by pitch class; the Full keyboard: the keys below the split key) */
    v->rq = (uint8_t)q;
    v->root = (int16_t)root;
    v->pc = (uint8_t)pc;
    v->nbase = (uint8_t)cr_chord_base_ex(root + v->transpose, (uint8_t)q, v->sx, c->sixth_up, v->base);
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

/* Chord and Bass Perform (0.34, GitHub issue 41): part CR_PF_CHORD performs the voice's voiced notes on the MAIN
 * stream in perform_mode / par, part CR_PF_BASS the same chord moved into the bass (crx_pf_notes) on the BASS stream in
 * bperf_mode / bpar. Each has its own owner, clock and scheduler owners (CR_OWN_V / CR_OWN_C carry the part) */
static int crx_bass_pitch(const cr_t *c, const cr_voice_t *v);
/* Follow (after 0.34, bfollow): the bass in the chord's mode and parameters, performing only while the chord part does */
static int crx_pf_on(const cr_t *c, int part)
{
    return c->perform_on && ((c->perf_parts >> part) & 1u) && !(part && c->bfollow && !(c->perf_parts & 1u));
}
static int crx_pf_mode(const cr_t *c, int part) { return part && !c->bfollow ? c->bperf_mode : c->perform_mode; }
static const int16_t *crx_pf_par(const cr_t *c, int part, int mode) { return part && !c->bfollow ? c->bpar[mode] : c->par[mode]; }
static int crx_pf_stream(int part) { return part ? CR_STREAM_BASS : CR_STREAM_MAIN; }
/* the notes a part performs: the chord's voiced notes; the bass's: its unvoiced notes (root position) moved by whole
 * octaves so that the root lands nearest the note the bass plays without Perform (crx_bass_pitch) */
static int crx_pf_notes(const cr_t *c, const cr_voice_t *v, int part, uint8_t *out)
{
    int i, n = 0, d;
    if (!part) {
        for (i = 0; i < v->nnotes; i++) out[i] = v->notes[i];
        return v->nnotes;
    }
    d = crx_bass_pitch(c, v) - (v->root + v->transpose) + 132;   /* (+11 octaves: a positive number) */
    d = d / 12 * 12 + (d % 12 >= 6 ? 12 : 0) - 132;                /* the nearest whole octave */
    for (i = 0; i < v->nbase; i++) {
        int nn = v->base[i] + d;
        if (nn >= 0 && nn <= 127) out[n++] = (uint8_t)nn;
    }
    return n;
}

/* scheduler dispatch targets; a stale event on a hushed voice is a no-op */
static void crx_perf_note_off(cr_t *c, int vi, int part, int note)
{
    cr_voice_t *v = &c->v[vi];
    cr_own_t *o = &v->pf[part].own;
    int i;
    if (!v->used || !o->live) return;   /* voice hushed in the same tick */
    for (i = 0; i < o->n; i++)
        if (o->note[i] == note) {
            for (; i + 1 < o->n; i++) o->note[i] = o->note[i + 1];
            o->n--;
            crx_note_end(c, crx_pf_stream(part), note);
            return;
        }
}

/* one strum/slop/harp note; reads velocity and (harp) gate length off the voice */
static void crx_strum_fire(cr_t *c, int vi, int part, int note)
{
    cr_voice_t *v = &c->v[vi];
    cr_pf_t *f = &v->pf[part];
    if (!v->used || !f->own.live) return;
    if (f->own.n >= CR_OWN_MAX || (f->gate_t && !crx_room(c, 1))) return;   /* never strand a note */
    if (crx_note_start(c, crx_pf_stream(part), note, v->vel)) {
        crx_own_append(&f->own, note);
        f->last_note = (uint8_t)note; f->has_last = 1;
        if (f->gate_t) crx_sched(c, f->gate_t, CR_EV_NOTE_OFF, CR_OWN_V(vi, part), vi, note);
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

static void crx_build_clocked_seq(cr_t *c, cr_voice_t *v, int part)
{
    cr_clk_t *k = &v->pf[part].clk;
    const int16_t *p = crx_pf_par(c, part, k->mode);
    uint8_t ex[CR_OWN_MAX], src[CR_CHORD_MAX];
    int ns = crx_pf_notes(c, v, part, src);
    int n = crx_expand_range(src, ns, p[CR_P_RANGE], ex);
    if (k->mode == CR_PM_ARP)
        k->nseq = (uint8_t)crx_order_notes(c, ex, n, p[CR_P_DIR], 1, src, ns, p[CR_P_RANGE], k->seq);
    else {
        int i;
        for (i = 0; i < n; i++) k->seq[i] = ex[i];
        k->nseq = (uint8_t)n;
    }
}

static void crx_fire_clocked_once(cr_t *c, int vi, int part)
{
    cr_voice_t *v = &c->v[vi];
    cr_pf_t *f = &v->pf[part];
    const int16_t *p = crx_pf_par(c, part, f->clk.mode);
    uint32_t du = CR_DIV_UNITS[p[CR_P_DIV]];
    int n = -1, vel = v->vel;
    if (f->clk.mode == CR_PM_ARP) {
        if (f->clk.nseq) {
            if (p[CR_P_DIR] == CR_DIR_RANDOM) n = f->clk.seq[crx_randn(c, f->clk.nseq)];
            else n = f->clk.seq[f->clk.pos % f->clk.nseq];
        }
    } else {
        int i = p[CR_P_PATTERN];
        uint32_t a = CR_PATTERNS[i - 1], len = (uint32_t)CR_PATTERNS[i] - a;   /* this pattern's byte range */
        int b = CR_PATTERNS[a - 1u + f->clk.pos % len], pi = b & 15;
        if (pi > 0 && f->clk.nseq) {
            n = f->clk.seq[crx_mod(pi - 1 + p[CR_P_ROTATE], f->clk.nseq)];
            vel = (vel * ((b >> 4) + 1) + 8) / 16;   /* accent scaling */
            if (vel < 1) vel = 1;
        }
    }
    if (n >= 0 && f->own.live && f->own.n < CR_OWN_MAX && crx_room(c, 1)) {
        if (crx_note_start(c, crx_pf_stream(part), n, vel)) {
            uint32_t bpm = c->bpm, dur = (2u * 25u * du * (uint32_t)p[CR_P_GATE] + bpm) / (2u * bpm);
            crx_own_append(&f->own, n);
            f->last_note = (uint8_t)n; f->has_last = 1;
            crx_sched(c, dur ? dur : 1u, CR_EV_NOTE_OFF, CR_OWN_V(vi, part), vi, n);
        }
    }
    f->clk.pos++;
    f->clk.step++;
}

/* fire every step that is due (a late tick catches up in order), then queue the next */
static void crx_clocked_step(cr_t *c, int vi, int part)
{
    int guard;
    for (guard = 0; guard < 64; guard++) {
        cr_voice_t *v = &c->v[vi];
        cr_clk_t *k = &v->pf[part].clk;
        const int16_t *p;
        uint32_t du, sw, bpm = c->bpm, due;
        if (!v->used || !k->on) return;
        p = crx_pf_par(c, part, k->mode);
        du = CR_DIV_UNITS[p[CR_P_DIV]];
        sw = (uint32_t)p[CR_P_SWING];
        if (k->step >= 2u) {   /* fold whole swing pairs into the anchor (exact, keeps numbers small) */
            uint32_t m = k->step >> 1, total = m * 5000u * du + k->rem;
            k->anchor += total / bpm;
            k->rem = (uint16_t)(total % bpm);
            k->step -= m * 2u;
        }
        due = crx_due_at(k->anchor, k->rem, k->step, du, sw, bpm);
        if ((int32_t)(due - c->now) <= 0) { crx_fire_clocked_once(c, vi, part); continue; }
        crx_sched(c, due - c->now, CR_EV_CLOCK, CR_OWN_C(vi, part), vi, 0);
        return;
    }
    crx_sched(c, 1, CR_EV_CLOCK, CR_OWN_C(vi, part), vi, 0);   /* runaway catch-up: continue next tick */
}

/* retrig ON anchors at the trigger; OFF at the global grid (chords substitute into the running phase) */
static void crx_phase_clocked(cr_t *c, cr_voice_t *v, int part, int setpos)
{
    cr_clk_t *ck = &v->pf[part].clk;
    const int16_t *p = crx_pf_par(c, part, ck->mode);
    if (p[CR_P_RETRIG]) {
        ck->anchor = c->now; ck->rem = 0; ck->step = 0;
    } else {
        uint32_t du = CR_DIV_UNITS[p[CR_P_DIV]], sw = (uint32_t)p[CR_P_SWING], bpm = c->bpm, k = 0;
        int32_t el = (int32_t)(c->now - c->clk_anchor);
        ck->anchor = c->clk_anchor; ck->rem = c->clk_rem;
        if (el > 0) {
            k = (uint32_t)el * bpm / (du * 2500u);
            k = k > 2u ? k - 2u : 0u;
        }
        while ((int32_t)(crx_due_at(ck->anchor, ck->rem, k, du, sw, bpm) - c->now) <= 0) k++;
        ck->step = k;
        if (setpos) ck->pos = k + c->clk_units / du;
    }
}

static void crx_rephase_clocked(cr_t *c, int vi, int part)
{
    cr_voice_t *v = &c->v[vi];
    if (!v->used || !v->pf[part].clk.on) return;
    crx_cancel(c, CR_OWN_C(vi, part));
    crx_phase_clocked(c, v, part, 0);
    crx_clocked_step(c, vi, part);
}

static void crx_start_clocked(cr_t *c, int vi, int part)
{
    cr_voice_t *v = &c->v[vi];
    cr_clk_t *k = &v->pf[part].clk;
    k->on = 1;
    k->mode = (uint8_t)crx_pf_mode(c, part);
    k->pos = 0; k->step = 0;
    k->anchor = c->now; k->rem = 0;
    crx_build_clocked_seq(c, v, part);
    crx_phase_clocked(c, v, part, 1);
    crx_clocked_step(c, vi, part);
}

static int crx_clocked_no_retrig(const cr_t *c, const cr_voice_t *v, int part)
{
    const cr_clk_t *k = &v->pf[part].clk;
    return k->on && !crx_pf_par(c, part, k->mode)[CR_P_RETRIG];
}

/* strum/slop sustain until release; harp gates; slop jitters timing only. Rates are 120-BPM reference
 * intervals scaled by the tempo (design.md 11). */
static void crx_start_performance(cr_t *c, int vi, int part)
{
    cr_voice_t *v = &c->v[vi];
    cr_pf_t *f = &v->pf[part];
    int mode = crx_pf_mode(c, part), n, i, rate, amt, ns;
    const int16_t *p = crx_pf_par(c, part, mode);
    uint8_t ex[CR_OWN_MAX], seq[CR_SEQ_MAX], src[CR_CHORD_MAX];
    f->own.live = 1; f->own.n = 0;
    f->has_last = 0;
    if (mode == CR_PM_ARP || mode == CR_PM_PATTERN) { crx_start_clocked(c, vi, part); return; }
    ns = crx_pf_notes(c, v, part, src);
    n = crx_expand_range(src, ns, p[CR_P_RANGE], ex);
    n = crx_order_notes(c, ex, n, p[CR_P_DIR], 0, src, ns, p[CR_P_RANGE], seq);
    rate = (p[CR_P_RATE] * 120 + c->bpm / 2) / c->bpm;
    if (rate < 1) rate = 1;
    amt = mode == CR_PM_SLOP ? p[CR_P_AMOUNT] : 0;
    f->gate_t = 0;
    if (mode == CR_PM_HARP) {
        int g = (rate * p[CR_P_GATE] + 50) / 100;
        f->gate_t = (uint16_t)(g < 1 ? 1 : g);
    }
    for (i = 0; i < n; i++) {
        int due = i * rate;
        if (amt > 0) {
            int r = (rate * amt + 50) / 100;
            due += crx_randn(c, 2 * r + 1) - r;
        }
        if (due <= 0) crx_strum_fire(c, vi, part, seq[i]);
        else crx_sched(c, (uint32_t)due, CR_EV_STRUM, CR_OWN_V(vi, part), vi, seq[i]);
    }
}
/* a part's performance stops: its queued steps and notes cancelled, what it sounds ended */
static void crx_stop_performance(cr_t *c, int vi, int part)
{
    cr_pf_t *f = &c->v[vi].pf[part];
    int i;
    if (f->clk.on) crx_cancel(c, CR_OWN_C(vi, part));
    if (f->own.live || part == CR_PF_CHORD) crx_cancel(c, CR_OWN_V(vi, part));   /* (the chord's: always, as ever) */
    for (i = 0; i < f->own.n; i++) crx_note_end(c, crx_pf_stream(part), f->own.note[i]);
    f->own.n = 0;
    f->own.live = 0; f->clk.on = 0;
}

/* ===================================================== streams, bass === */
/* Bass Behaviour (design.md 9.1; Choralroot interpretation, parity unverified): applies while Bass is on.
 * Solo silences main and raw; Single Notes plays main/raw only for chords; Chords Only plays bass only
 * for chords; Unison plays bass for everything at the played pitch. */
static int crx_chord_streams_ok(const cr_t *c, const cr_voice_t *v)
{
    if (v->zone & CR_Z_BASS) return 0;                   /* Split Bass, the bass side: the BASS stream alone */
    if (c->single == CR_SINGLE_SPLIT_BASS) return 1;     /* .. the Bass Behaviour is ignored meanwhile */
    if (!c->bass_on) return 1;
    if (c->bass_mode == CR_BASS_SOLO) return 0;
    if (c->bass_mode == CR_BASS_SINGLE_NOTES && v->rq == CR_Q_NONE) return 0;
    return 1;
}
static int crx_main_ok(const cr_t *c, const cr_voice_t *v) { return c->stream_on[CR_STREAM_MAIN] && crx_chord_streams_ok(c, v); }
static int crx_raw_ok(const cr_t *c, const cr_voice_t *v) { return c->stream_on[CR_STREAM_RAW] && crx_chord_streams_ok(c, v); }
static int crx_bass_wanted(const cr_t *c, const cr_voice_t *v)
{
    if (c->single == CR_SINGLE_SPLIT_BASS) return (v->zone & CR_Z_BASS) != 0;   /* played by hand: nothing follows */
    return !(c->bass_mode == CR_BASS_CHORDS_ONLY && v->rq == CR_Q_NONE);
}
static int crx_bass_ok(const cr_t *c, const cr_voice_t *v)
{
    if (v->zone & CR_Z_BASS) return c->stream_on[CR_STREAM_BASS];   /* (whatever Bass on / off says) */
    return c->bass_on && c->stream_on[CR_STREAM_BASS] && crx_bass_wanted(c, v);
}
static int crx_bass_pitch(const cr_t *c, const cr_voice_t *v)
{
    if (c->bass_mode == CR_BASS_UNISON || (v->zone & CR_Z_BASS)) return v->note_pressed + v->transpose + 12 * c->bass_voicing;
    /* Solo (GitHub issues 40, 43): the root in the octave it was played in, the register moving it by octaves; until
     * 0.36 it was the bass register's, so every octave of the keys sounded the same one */
    if (c->bass_mode == CR_BASS_SOLO) return v->root + v->transpose + 12 * c->bass_voicing;
    return (CR_BASS_OCT + c->bass_voicing + 1) * 12 + v->pc + v->transpose;   /* root pc in the bass register */
}
/* the bass follows voice vi: its note, or with the Bass Perform on its performance (crx_pf_notes) */
static void crx_bass_start(cr_t *c, int vi)
{
    cr_voice_t *v = &c->v[vi];
    int p;
    v->bass.live = 1; v->bass.n = 0;
    if (crx_pf_on(c, CR_PF_BASS)) { crx_start_performance(c, vi, CR_PF_BASS); return; }
    p = crx_bass_pitch(c, v);
    if (crx_note_start(c, CR_STREAM_BASS, p, v->vel)) { v->bass.n = 1; v->bass.note = (uint8_t)p; }
}
static void crx_bass_release(cr_t *c, int vi)
{
    cr_voice_t *v = &c->v[vi];
    if (v->bass.n) crx_note_end(c, CR_STREAM_BASS, v->bass.note);
    v->bass.n = 0;
    if (v->pf[CR_PF_BASS].own.live) crx_stop_performance(c, vi, CR_PF_BASS);
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
    cr_own_t *o = &v->pf[CR_PF_CHORD].own;
    if (!on) {
        crx_stop_performance(c, vi, CR_PF_CHORD);
    } else if (crx_main_ok(c, v)) {
        if (crx_pf_on(c, CR_PF_CHORD)) crx_start_performance(c, vi, CR_PF_CHORD);
        else {
            int i;
            o->live = 1; o->n = 0;
            for (i = 0; i < v->nnotes; i++)
                if (crx_note_start(c, CR_STREAM_MAIN, v->notes[i], v->vel)) crx_own_append(o, v->notes[i]);
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
    c->out.gesture(c->out.ud, v->gid, (int16_t)(v->root + v->transpose), v->rq,
                   (uint8_t)(v->zone & CR_Z_BASS ? CR_GX_BASS : v->sx), v->vel, on);   /* (a bass-side key: q, sx 0) */
}

static void crx_kill_voice(cr_t *c, int vi)
{
    cr_voice_t *v = &c->v[vi];
    int i, best = -1;
    crx_gesture(c, vi, 0);
    crx_hush_voice(c, vi);
    crx_bass_release(c, vi);
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

/* the voice of a key: a played one before a Free one (Free can sound the last root again while that key's own voice
 * still sounds, the Full keyboard's note: its release must end the played one, not the Free copy) */
static int crx_find_voice(const cr_t *c, int key)
{
    int i, f = -1;
    for (i = 0; i < CR_MAX_VOICES; i++)
        if (c->v[i].used && c->v[i].key == key) {
            if (!c->v[i].is_free) return i;
            if (f < 0) f = i;
        }
    return f;
}

/* the voicing a voice takes: a chord's (the Full keyboard's Key chords too); none for a single note of the Full
 * keyboard or a bass-side note (nothing to rotate; KNOB 1 is the octave on the Full keyboard) */
static int crx_voic(const cr_t *c, const cr_voice_t *v)
{
    return v->zone && !((v->zone & CR_Z_FULL) && v->rq) ? 0 : c->voicing;
}
/* build, register and sound a voice; bass follows it */
static int crx_start_voice(cr_t *c, int key, int id, int vel, int pc_pressed, int note_pressed, int sq, int sx, int is_free,
                           int zone)
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
    v->zone = (uint8_t)zone;
    v->serial = ++c->serial;
    v->gid = ++c->gid;
    crx_build_base(c, v);
    v->nnotes = (uint8_t)cr_voice_apply(v->base, v->nbase, crx_voic(c, v), v->notes);
    crx_sound_voice(c, vi);
    if (crx_bass_ok(c, v)) crx_bass_start(c, vi);
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
    return c->sticky || (crx_pf_on(c, CR_PF_CHORD) && c->par[c->perform_mode][CR_P_HOLD]) ||
           (crx_pf_on(c, CR_PF_BASS) && crx_pf_par(c, CR_PF_BASS, crx_pf_mode(c, CR_PF_BASS))[CR_P_HOLD]);   /* (a Hold of either part latches the
                                                                                * gesture: both parts follow it) */
}
/* Snap: a held key plays this note (not its own) */
static int crx_snapped_to(const cr_t *c, int note)
{
    int i;
    if (c->snapn_n)
        for (i = 0; i < 128; i++)
            if (c->snapn[i] == note + 1) return 1;
    return 0;
}
/* a key held plays this note as itself: down, and Snap has not moved it (a key pressed again under Snap plays another
 * note: the voice it played as itself before is no longer its, and its release will not end it) */
static int crx_held_as_itself(const cr_t *c, int note, int id) { return (c->phys[note] & (1u << id)) && !c->snapn[note]; }
static void crx_latch_sync(cr_t *c)
{
    int i;
    if (cr_latching(c)) return;
    for (i = 0; i < CR_MAX_VOICES; i++) {
        const cr_voice_t *v = &c->v[i];
        if (v->used && !crx_held_as_itself(c, v->key, v->id) && !crx_snapped_to(c, v->key))
            crx_kill_voice(c, i);   /* latched, not held */
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

/* the voice's chord changed: the bass starts or stops by its Behaviour; a Bass Perform running follows the new chord
 * (a clocked one without Retrig into its running phase, as the chord's) */
static void crx_bass_sync(cr_t *c, int vi)
{
    cr_voice_t *v = &c->v[vi];
    int want;
    if (v->zone & CR_Z_BASS) return;                            /* (a bass-side key: its note is the bass) */
    if (!c->bass_on || !c->stream_on[CR_STREAM_BASS]) return;
    want = crx_bass_wanted(c, v);
    if (want && !v->bass.live) crx_bass_start(c, vi);
    else if (!want && v->bass.live) { crx_bass_release(c, vi); v->bass.live = 0; }
    else if (v->bass.live && v->pf[CR_PF_BASS].own.live) {
        if (crx_clocked_no_retrig(c, v, CR_PF_BASS)) crx_build_clocked_seq(c, v, CR_PF_BASS);
        else { crx_bass_release(c, vi); crx_bass_start(c, vi); }
    }
}

/* play styles (design.md 7). Simple: spec frozen at note-on. Advanced: modifier presses mutate held
 * voices. Free: also retriggers the last state when no root is held. */
static void crx_transform_voice(cr_t *c, int vi, int reattack)
{
    cr_voice_t *v = &c->v[vi];
    crx_build_base(c, v);
    v->nnotes = (uint8_t)cr_voice_apply(v->base, v->nbase, crx_voic(c, v), v->notes);
    crx_note_last_chord(c, vi);
    /* Full re-strike (not performing): Chord Type press (reattack) or Play Chord; else Add Note diffs. */
    if (!crx_pf_on(c, CR_PF_CHORD) && (reattack || c->extadd == CR_EXTADD_PLAY_CHORD)) {
        crx_hush_voice(c, vi); crx_sound_voice(c, vi);
        crx_bass_sync(c, vi);
        return;
    }
    if (c->stream_on[CR_STREAM_RAW]) {
        if (crx_chord_streams_ok(c, v))
            crx_sync_owner(c, CR_STREAM_RAW, &v->raw.live, v->raw.note, &v->raw.n, CR_CHORD_MAX, v->notes, v->nnotes, v->vel);
        else { crx_raw_release(c, v); v->raw.live = 0; }
    }
    if (crx_pf_on(c, CR_PF_CHORD)) {
        if (crx_clocked_no_retrig(c, v, CR_PF_CHORD)) crx_build_clocked_seq(c, v, CR_PF_CHORD);   /* substitute into the
                                                                                                  * running phase */
        else { crx_main_voice(c, vi, 0); crx_main_voice(c, vi, 1); }
        crx_bass_sync(c, vi);
        return;
    }
    if (c->stream_on[CR_STREAM_MAIN]) {
        cr_own_t *o = &v->pf[CR_PF_CHORD].own;
        if (crx_chord_streams_ok(c, v))
            crx_sync_owner(c, CR_STREAM_MAIN, &o->live, o->note, &o->n, CR_OWN_MAX, v->notes, v->nnotes, v->vel);
        else crx_main_voice(c, vi, 0);
    }
    crx_bass_sync(c, vi);
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
        if (!v->used || v->zone) continue;   /* (a Full-keyboard / bass-side note has no chord to change) */
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
        crx_start_voice(c, lp.key, lp.id, lp.vel, lp.pc_pressed, lp.note_pressed, sq, sx, 1, 0);
    }
}

static void crx_modifier_release(cr_t *c)
{
    crx_rec_commit(c);
    if (c->mods_down) return;   /* Free voices need a held modifier */
    crx_clear_free_voices(c);
}

/* LOCK (cr_mod_lock): the chord keys down are the UI's set; Advanced / Free: every chord voice takes it (the type
 * and the extensions snapshotted as a new root would), re-struck when a type key's press changed its type. No
 * toggle: the engine's own (crx_modifier_press: v->sx ^= bit, kept after the key's up) ran on top of the latch, so
 * with a root held a latched extension's second push switched it back ON, and Free's re-sounded chord (never let
 * go: a latched key is never up) collected every extension ever pushed (lp.sx | bit) */
static void crx_lock_sync(cr_t *c, int reattack)
{
    uint8_t sq, sx;
    int i;
    if (c->playstyle == CR_PS_SIMPLE) return;
    crx_snapshot_spec(c, &sq, &sx);
    for (i = 0; i < CR_MAX_VOICES; i++) {
        cr_voice_t *v = &c->v[i];
        int typed;
        if (!v->used || v->zone || (v->sq == sq && v->sx == sx)) continue;
        typed = v->sq != sq;
        v->sq = sq; v->sx = sx;
        crx_transform_voice(c, i, reattack && typed);
    }
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
    c->perf_parts = 1u << CR_PF_CHORD;   /* the chord performs, the bass not (as before 0.34) */
    c->bperf_mode = CR_PM_STRUM;
    c->bpm = 120;
    c->single = CR_SINGLE_FULL;
    c->split_pc = 5;
    c->split_note = 65;              /* F4: the split key (cr_set_keyboard) */
    c->stream_on[CR_STREAM_MAIN] = 1;
    c->stream_on[CR_STREAM_BASS] = 1;
    c->stream_on[CR_STREAM_RAW] = 0;
    for (m = 0; m < CR_PM_COUNT; m++)
        for (p = 0; p < CR_P_COUNT; p++) c->par[m][p] = c->bpar[m][p] = CR_PAR_DEFAULT[m][p];
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

/* MIDI Start (3y253): the grid restarts now and every clocked voice (Arp, Pattern) starts again from its first step,
 * the downbeat with the master's */
__attribute__((noinline)) void cr_transport_start(cr_t *c)
{
    int i;
    cr_clock_reset(c);
    for (i = 0; i < CR_NVOICE * CR_NPF; i++) {
        int vi = i >> 1, part = i & 1;
        cr_clk_t *k = &c->v[vi].pf[part].clk;
        if (!c->v[vi].used || !k->on) continue;
        crx_cancel(c, CR_OWN_C(vi, part));
        k->pos = 0; k->step = 0;                 /* step 0 due now, on the new grid (crx_phase_clocked would take the */
        k->anchor = c->clk_anchor;               /* next one: a step due now counts as passed there) */
        k->rem = c->clk_rem;
        crx_clocked_step(c, vi, part);
    }
}

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
        int part = (int)CR_OWN_PART(e.owner);
        if (e.kind == CR_EV_NOTE_OFF) crx_perf_note_off(c, e.vi, part, e.note);
        else if (e.kind == CR_EV_STRUM) crx_strum_fire(c, e.vi, part, e.note);
        else if (e.kind == CR_EV_CLOCK) {
            cr_voice_t *v = &c->v[e.vi];
            if (v->used && v->pf[part].clk.on) { crx_fire_clocked_once(c, e.vi, part); crx_clocked_step(c, e.vi, part); }
        }
    }
    for (k = d; k < c->nev; k++) c->ev[k - d] = c->ev[k];
    c->nev = (uint16_t)(c->nev - d);
}

void cr_key_ex(cr_t *c, uint8_t note, uint8_t id, uint8_t vel, int down)
{
    uint8_t bit, raw = note;
    int vi, bkeep;
    if (note > 127) return;
    id &= 7u;
    bit = (uint8_t)(1u << id);
    if (down) {
        cr_voice_t *v;
        /* Key plays Notes, Key Mode on (either keyboard): a key out of the scale does nothing at all (its release
         * finds no press: phys is not set), or, with Snap, plays the nearest note of the target (cr_snap_note): the
         * key keeps its own place in phys, the voice is the played note's, snapn says which for the release. (Key
         * plays Chords: the key's alternate chord, crx_build_base; the Full keyboard too since 0.22) */
        if (c->key_on && c->key_notes && !((cr_scale_mask(c) >> (note % 12u)) & 1u)) {
            int sn = cr_snap_note(c, note);
            if (sn < 0) return;
            note = (uint8_t)sn;
        }
        /* Once every key of a latched gesture has been lifted, the first press of the next gesture replaces
         * the latched group instead of adding to it. */
        if (cr_latching(c) && c->phys_n == 0) crx_clear_manual_voices(c);
        if (!(c->phys[raw] & bit)) { c->phys[raw] |= bit; c->phys_n++; }
        if (note != raw && !c->snapn[raw]) { c->snapn[raw] = (uint8_t)(note + 1u); c->snapn_n++; }
        vi = crx_find_voice(c, note);
        if (vi >= 0 && c->v[vi].is_free) { crx_kill_voice(c, vi); vi = -1; }   /* a real press replaces a Free voice */
        if (vi >= 0) {
            /* same root: release, retrigger the same chord at the new velocity (design.md 5.3) */
            v = &c->v[vi];
            crx_gesture(c, vi, 0);   /* a looper: one gesture ends, the retrigger begins another */
            v->gid = ++c->gid;
            bkeep = crx_clocked_no_retrig(c, v, CR_PF_BASS);   /* (a Bass Perform's arp, Retrig off: continues) */
            if (!bkeep) crx_bass_release(c, vi);
            v->superseded |= (uint8_t)(1u << v->id);
            v->id = id;
            v->vel = vel;
            if (!crx_clocked_no_retrig(c, v, CR_PF_CHORD)) { crx_hush_voice(c, vi); crx_sound_voice(c, vi); }   /* retrig
                                                                                                              * off: continues */
            if (v->bass.live && !bkeep) crx_bass_start(c, vi);
            crx_gesture(c, vi, 1);
        } else {
            uint8_t sq, sx;
            int zone = c->full ? CR_Z_FULL : 0;
            /* below the split: the Chords layout by pitch class (split_pc, as Split Oct there), the Full keyboard
             * by key (split_note) */
            if (c->single == CR_SINGLE_SPLIT_BASS && (c->full ? note < c->split_note : note % 12 < c->split_pc))
                zone |= CR_Z_BASS;
            else if (c->full && c->single == CR_SINGLE_SPLIT && note < c->split_note) zone |= CR_Z_LOW;
            crx_snapshot_spec(c, &sq, &sx);   /* spec captured at note-on */
            if (zone && !(c->full_chords == 1 && !(zone & CR_Z_BASS)))
                sq = sx = 0;                  /* the Full keyboard (no chord key: Key plays Chords gives the key's
                                               * chord in crx_build_base), a bass-side key: a single note. G5's chords
                                               * on every key (cr_set_full_chords): the chord keys down, as a root */
            vi = crx_start_voice(c, note, id, vel, note % 12, note, sq, sx, 0, zone);
            if (vi < 0) return;
            c->lp.valid = 1; c->lp.attached = 1; c->lp.vi = (uint8_t)vi; c->lp.serial = c->v[vi].serial;
        }
        crx_note_last_chord(c, vi);
    } else {
        if (c->phys[raw] & bit) { c->phys[raw] &= (uint8_t)~bit; c->phys_n--; }
        crx_rec_commit(c);
        if (c->snapn[raw]) {                      /* a key Snap moved: the note it plays (the key, the chord, Snap may
                                                   * have changed since) ... */
            note = (uint8_t)(c->snapn[raw] - 1u);
            c->snapn[raw] = 0;
            c->snapn_n--;
            if (crx_held_as_itself(c, note, id)) return;   /* .. held as itself too: it stays */
        }
        if (crx_snapped_to(c, note)) return;      /* another held key plays it: it stays */
        vi = crx_find_voice(c, note);
        if (cr_latching(c)) {                     /* it sounds on: a looper is told the key is up (CR_G_KEYUP) */
            if (vi >= 0 && c->v[vi].id == id && !c->v[vi].is_free) crx_gesture(c, vi, CR_G_KEYUP);
            return;
        }
        if (vi < 0) return;
        if (c->v[vi].id == id) crx_kill_voice(c, vi);
        else if (c->v[vi].superseded & bit) c->v[vi].superseded &= (uint8_t)~bit;   /* superseded press: ignored */
    }
}

void cr_key(cr_t *c, uint8_t midi_note, uint8_t vel, int down) { cr_key_ex(c, midi_note, 0, vel, down); }

void cr_mod_ex(cr_t *c, cr_mod_t m, int down, int quiet)
{
    int mi = (int)m, i;
    uint8_t bit;
    if (mi < 0 || mi >= CR_NMOD) return;
    bit = (uint8_t)(1u << mi);
    if (down == CR_MOD_PRESS) {                    /* (a press of a mod held already: what its down would do) */
        if (!(c->mods_down & bit)) return;
        if (!c->lock) crx_modifier_press(c, mi);
    } else if (down) {
        if (!(c->mods_down & bit)) {
            c->mods_down |= bit;
            if (mi <= CR_MOD_SUS && c->qn < CR_NMOD) c->qstack[c->qn++] = (uint8_t)(mi + 1);
            if (!c->lock && down != CR_MOD_HELD) crx_modifier_press(c, mi);
        }
    } else if (c->mods_down & bit) {
        c->mods_down &= (uint8_t)~bit;
        if (mi <= CR_MOD_SUS)
            for (i = (int)c->qn - 1; i >= 0; i--)
                if (c->qstack[i] == mi + 1) {
                    for (; i + 1 < c->qn; i++) c->qstack[i] = c->qstack[i + 1];
                    c->qn--;
                    break;
                }
        if (c->lock) crx_rec_commit(c);
        else crx_modifier_release(c);
    }
    /* LOCK: the voices follow the set, once per change (a second down of a latched key too: the last of a batch) */
    if (c->lock && !quiet && down != CR_MOD_HELD) crx_lock_sync(c, down && mi <= CR_MOD_SUS);
}
void cr_mod(cr_t *c, cr_mod_t m, int down) { cr_mod_ex(c, m, down, 0); }

void cr_mod_lock(cr_t *c, int on)
{
    on = on != 0;
    if (c->lock == (uint8_t)on) return;
    c->lock = (uint8_t)on;
    c->mods_touch = 0;
    if (!on && !c->mods_down) crx_clear_free_voices(c);   /* (as crx_modifier_release: no key down any more) */
}

/* LOCK, Free: a chord key pressed with no voice sounds the last root again as the set's chord (cr_mod before this:
 * the set is the new one; a lone repeat changes it only at its release: the old set until then); the last chord key
 * let go ends it (a latched key stays in mods_down: crx_modifier_release's rule never would) */
void cr_mod_touch(cr_t *c, uint8_t held)
{
    uint8_t pressed = (uint8_t)(held & ~c->mods_touch);
    int i;
    c->mods_touch = held;
    if (!c->lock || c->playstyle != CR_PS_FREE) return;
    if (!held) { crx_clear_free_voices(c); return; }
    if (!pressed || !c->lp.valid) return;
    for (i = 0; i < CR_MAX_VOICES; i++)
        if (c->v[i].used) return;                  /* a root held, or the Free voice already: crx_lock_sync's */
    {
        cr_lastplayed_t lp;
        uint8_t sq, sx;
        crx_lp_get(c, &lp);
        crx_snapshot_spec(c, &sq, &sx);
        crx_start_voice(c, lp.key, lp.id, lp.vel, lp.pc_pressed, lp.note_pressed, sq, sx, 1, 0);
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
        if (!v->used || (v->zone && !((v->zone & CR_Z_FULL) && v->rq)) || (v->loop & CR_LOOP_FIXV))
            continue;                              /* (a loop step with its own voicing; a note with nothing to rotate) */
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
        if (v->used && v->bass.live) { crx_bass_release(c, i); crx_bass_start(c, i); }
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
/* a part's performance changed (Perform on / off, the part on / off, its mode): every voice sounding plays it anew */
static void crx_restart_sounding_voices(cr_t *c, int part)
{
    int i;
    for (i = 0; i < CR_NVOICE; i++) {
        if (!c->v[i].used) continue;
        if (part == CR_PF_CHORD) { crx_main_voice(c, i, 0); crx_main_voice(c, i, 1); }
        else if (c->v[i].bass.live) { crx_bass_release(c, i); crx_bass_start(c, i); }
    }
}

void cr_set_playstyle(cr_t *c, cr_playstyle_t p)
{
    if ((int)p < CR_PS_SIMPLE || (int)p > CR_PS_FREE || c->playstyle == (uint8_t)p) return;
    c->playstyle = (uint8_t)p;
    c->lp.valid = 0;   /* new semantics invalidate Free state (design.md 7/17) */
    crx_clear_free_voices(c);
}
void cr_set_ext_addition(cr_t *c, cr_extadd_t e) { c->extadd = (uint8_t)(e == CR_EXTADD_PLAY_CHORD); }
/* Options > 6th with 7th (0.20): the chords built from now on (a key, a pad, a loop step, a chord key's change of a
 * held chord); a chord that sounds keeps its notes, and its note-offs are its owners' own, whatever this says then */
void cr_set_sixth_up(cr_t *c, int on) { c->sixth_up = (uint8_t)(on != 0); }
void cr_set_secret(cr_t *c, cr_secret_t s) { if ((int)s >= CR_SECRET_OFF && (int)s <= CR_SECRET_ALL) c->secret = (uint8_t)s; }
void cr_set_key(cr_t *c, int enabled, uint8_t tonic_pc, cr_scale_t scale)
{
    c->key_on = (uint8_t)(enabled != 0);
    c->tonic = (uint8_t)(tonic_pc % 12u);
    c->scale = (uint8_t)((unsigned)scale < CR_SCALE_COUNT ? scale : CR_SCALE_MAJOR);
}
void cr_set_transpose(cr_t *c, int semis) { c->transpose = (int8_t)crx_clamp(semis, -24, 24); }
void cr_set_single_notes(cr_t *c, cr_single_t mode, int split_pc)
{
    c->single = (uint8_t)((int)mode >= CR_SINGLE_FULL && (int)mode <= CR_SINGLE_SPLIT_BASS ? mode : CR_SINGLE_FULL);
    c->split_pc = (uint8_t)crx_clamp(split_pc, 0, 11);
}
void cr_set_key_plays(cr_t *c, int notes) { c->key_notes = (uint8_t)(notes != 0); }
void cr_set_key_snap(cr_t *c, cr_keysnap_t mode)
{
    c->snap = (uint8_t)((int)mode >= CR_KEYSNAP_OFF && (int)mode <= CR_KEYSNAP_CHORD ? mode : CR_KEYSNAP_OFF);
}
/* Snap (0.22, GitHub issue 18): the target as a pitch-class mask. Scale: the scale's notes. Chord: the base notes of
 * every voice sounding with more than one (the player's chord, a pad's, a loop's), each taken back by its own
 * captured transpose (the keys arrive before Transpose: the tone comes out where it sounds); the scale's when none.
 * Then the nearest: d = 1 .. 11, down before up, so ties go down (C# in C major plays C; over C E G, D# plays E, A#
 * plays C). A chord tone the key already is (out of the scale: a borrowed chord's) plays as itself */
int cr_snap_note(const cr_t *c, uint8_t note)
{
    uint16_t m = 0;
    int d, i, k;
    if (!c->snap || !c->key_on || note > 127) return -1;
    if (c->snap == CR_KEYSNAP_CHORD)
        for (i = 0; i < CR_NVOICE; i++) {
            const cr_voice_t *v = &c->v[i];
            if (!v->used || v->nbase < 2u) continue;
            for (k = 0; k < v->nbase; k++) m |= (uint16_t)(1u << crx_mod((int)v->base[k] - v->transpose, 12));
        }
    if (!m) m = cr_scale_mask(c);
    if (!m) return -1;
    if ((m >> (note % 12u)) & 1u) return note;
    for (d = 1; d < 12; d++) {
        if (note >= d && ((m >> ((note - d) % 12u)) & 1u)) return note - d;
        if (note + d <= 127 && ((m >> ((note + d) % 12u)) & 1u)) return note + d;
    }
    return -1;
}
void cr_set_keyboard(cr_t *c, int full, int split_note)
{
    c->full = (uint8_t)(full != 0);
    c->split_note = (uint8_t)crx_clamp(split_note, 0, 127);
}
void cr_set_full_chords(cr_t *c, int mode) { c->full_chords = (uint8_t)(mode == 1 || mode == 2 ? mode : 0); }
/* Bass on / off (0.34): a held chord's bass follows at once, its note or its Bass Perform stopping or starting */
void cr_set_bass(cr_t *c, int enabled)
{
    int i;
    enabled = enabled != 0;
    if (c->bass_on == enabled) return;
    c->bass_on = (uint8_t)enabled;
    for (i = 0; i < CR_NVOICE; i++) {
        cr_voice_t *v = &c->v[i];
        if (!v->used || (v->zone & CR_Z_BASS)) continue;   /* (a bass-side key plays whatever Bass says) */
        if (enabled) crx_bass_sync(c, i);
        else if (v->bass.live) { crx_bass_release(c, i); v->bass.live = 0; }
    }
}
void cr_set_bass_mode(cr_t *c, cr_bassmode_t m) { if ((int)m >= CR_BASS_CHORDS_ONLY && (int)m <= CR_BASS_SOLO) c->bass_mode = (uint8_t)m; }
void cr_set_stream(cr_t *c, cr_stream_t s, int enabled) { if ((int)s >= 0 && (int)s < CR_NSTREAM) c->stream_on[s] = (uint8_t)(enabled != 0); }

void cr_set_perform(cr_t *c, int enabled)
{
    int part;
    enabled = enabled != 0;
    if (c->perform_on == enabled) return;
    c->perform_on = (uint8_t)enabled;
    for (part = 0; part < CR_NPF; part++)
        if ((c->perf_parts >> part) & 1u) crx_restart_sounding_voices(c, part);   /* design.md 11.1/17 */
    crx_latch_sync(c);                /* disabling Perform releases any Hold-latched voices */
}

void cr_set_perform_part(cr_t *c, int part, int on)
{
    uint8_t m;
    if (part < 0 || part >= CR_NPF) return;
    m = (uint8_t)(on ? c->perf_parts | (1u << part) : c->perf_parts & ~(1u << part));
    if (m == c->perf_parts) return;
    c->perf_parts = m;
    if (c->perform_on) crx_restart_sounding_voices(c, part);
    if (c->perform_on && !part && c->bfollow && (m & 2u)) crx_restart_sounding_voices(c, CR_PF_BASS);   /* (a following
                                                                                                    * bass with it) */
    crx_latch_sync(c);   /* a part with Hold left: its latch goes */
}
void cr_set_perform_follow(cr_t *c, int on)
{
    on = on != 0;
    if (c->bfollow == on) return;
    c->bfollow = (uint8_t)on;
    if (c->perform_on && (c->perf_parts & 2u)) crx_restart_sounding_voices(c, CR_PF_BASS);
    crx_latch_sync(c);
}

void cr_set_perform_mode_ex(cr_t *c, int part, cr_pmode_t m)
{
    uint8_t *pm;
    if (part < 0 || part >= CR_NPF || (int)m < 0 || (int)m >= CR_PM_COUNT) return;
    pm = part ? &c->bperf_mode : &c->perform_mode;
    if (*pm == (uint8_t)m) return;
    *pm = (uint8_t)m;
    if (crx_pf_on(c, part)) crx_restart_sounding_voices(c, part);
    if (!part && c->bfollow && crx_pf_on(c, CR_PF_BASS)) crx_restart_sounding_voices(c, CR_PF_BASS);   /* (Follow) */
    crx_latch_sync(c);   /* leaving a Hold mode releases latched voices */
}
void cr_set_perform_mode(cr_t *c, cr_pmode_t m) { cr_set_perform_mode_ex(c, CR_PF_CHORD, m); }

/* Timing rephases in place; finite modes refresh from the held chord (design.md 17). */
void cr_set_param_ex(cr_t *c, int part, cr_pmode_t m, cr_param_t p, int value)
{
    int i, last;
    int16_t *pp;
    if (part < 0 || part >= CR_NPF || (int)m < 0 || (int)m >= CR_PM_COUNT || (int)p < 0 || (int)p >= CR_P_COUNT) return;
    value = crx_clamp(value, CR_PAR_MIN[p], CR_PAR_MAX[p]);
    pp = part ? &c->bpar[m][p] : &c->par[m][p];
    if (*pp == value) return;
    *pp = (int16_t)value;
    last = part == CR_PF_CHORD && c->bfollow ? CR_PF_BASS : part;   /* the chord's: a following bass's too */
    if (part == CR_PF_BASS && c->bfollow) last = -1;            /* the bass's own: unused while it follows */
    for (; part <= last; part++) {
        for (i = 0; i < CR_NVOICE; i++) {
            cr_voice_t *v = &c->v[i];
            cr_clk_t *k = &v->pf[part].clk;
            if (!v->used) continue;
            if (k->on) {
                if (k->mode != m) continue;
                if (p == CR_P_DIR || p == CR_P_RANGE) crx_build_clocked_seq(c, v, part);
                else if (p == CR_P_DIV || p == CR_P_SWING || p == CR_P_RETRIG) crx_rephase_clocked(c, i, part);
            } else if (crx_pf_on(c, part) && (int)m == crx_pf_mode(c, part)) {
                if (part == CR_PF_CHORD) { crx_main_voice(c, i, 0); crx_main_voice(c, i, 1); }
                else if (v->bass.live) { crx_bass_release(c, i); crx_bass_start(c, i); }
            }
        }
    }
    if (p == CR_P_HOLD) crx_latch_sync(c);   /* Hold turned off: release latched voices */
}
void cr_set_param(cr_t *c, cr_pmode_t m, cr_param_t p, int value) { cr_set_param_ex(c, CR_PF_CHORD, m, p, value); }
int cr_get_param_ex(const cr_t *c, int part, cr_pmode_t m, cr_param_t p)
{
    if (part < 0 || part >= CR_NPF || (int)m < 0 || (int)m >= CR_PM_COUNT || (int)p < 0 || (int)p >= CR_P_COUNT) return 0;
    return part ? c->bpar[m][p] : c->par[m][p];
}
int cr_get_param(const cr_t *c, cr_pmode_t m, cr_param_t p) { return cr_get_param_ex(c, CR_PF_CHORD, m, p); }
int cr_param_default(cr_pmode_t m, cr_param_t p)
{
    return (int)m >= 0 && (int)m < CR_PM_COUNT && (int)p >= 0 && (int)p < CR_P_COUNT ? CR_PAR_DEFAULT[m][p] : 0;
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
    for (i = 0; i < CR_NVOICE * CR_NPF; i++)
        if (c->v[i >> 1].used && c->v[i >> 1].pf[i & 1].clk.on) crx_rephase_clocked(c, i >> 1, i & 1);
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
    v->nbase = (uint8_t)cr_chord_base_ex(d->root + v->transpose, d->q, d->x, c->sixth_up, v->base);
    v->nnotes = (uint8_t)cr_voice_apply(v->base, v->nbase, c->voicing, v->notes);
    crx_sound_voice(c, vi);
    if (crx_bass_ok(c, v)) crx_bass_start(c, vi);
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
    return cr_loop_event_v(c, lv, root, q, ext, vel, on, CR_VOIC_FOLLOW);
}

int cr_loop_event_v(cr_t *c, int lv, int16_t root, uint8_t q, uint8_t ext, uint8_t vel, int on, int voic)
{
    int vi = CR_MAX_VOICES + CR_MAX_PADS + lv;
    cr_voice_t *v;
    if (lv < 0 || lv >= CR_MAX_LOOPV) return 0;
    if (c->v[vi].used) crx_kill_voice(c, vi);
    if (!on) return 0;
    if (q >= CR_Q_COUNT || (ext & CR_GX_BASS)) q = CR_Q_NONE;
    root = (int16_t)crx_clamp(root, 0, 127);
    v = &c->v[vi];
    crx_zero(v, sizeof *v);
    v->used = 1; v->loop = (uint8_t)(lv + 1);
    if (ext & CR_GX_BASS) { v->zone = CR_Z_BASS; ext = 0; }   /* recorded on the bass side: the bass part's note, as
                                                              * the key was (crx_bass_pitch: + the register of now) */
    v->vel = (uint8_t)crx_clamp(vel, 1, 127);
    v->note_pressed = (uint8_t)root;
    v->pc = v->pc_pressed = (uint8_t)crx_mod(root, 12);
    v->transpose = 0;                /* the recorded root is absolute */
    v->sq = v->rq = q; v->sx = (uint8_t)(ext & 15u);
    v->root = root;
    v->serial = ++c->serial;
    v->nbase = (uint8_t)cr_chord_base_ex(root, q, v->sx, c->sixth_up, v->base);
    if (voic != CR_VOIC_FOLLOW && q != CR_Q_NONE && !v->zone) {   /* the step's own voicing (a chord's only) */
        v->loop |= CR_LOOP_FIXV;
        voic = crx_clamp(voic, CR_VOICE_MIN, CR_VOICE_MAX);
    } else {
        voic = c->voicing;
    }
    v->nnotes = (uint8_t)cr_voice_apply(v->base, v->nbase, v->zone ? 0 : voic, v->notes);
    crx_sound_voice(c, vi);
    if (crx_bass_ok(c, v)) crx_bass_start(c, vi);
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
 * also Cm7 / Cdim7) except on sus ("m7"); 3 extensions = JAZZ, 4 = WTF.
 * ChoralRoot's one departure (the owner, 2026-10-12: "for Major chords, nothing shows, major chords should use capital M
 * to differentiate from single notes. So C major would read CM"): a plain major triad, no extension, prints "M" at
 * root size ("C" "M"), so "C" alone is always a single note (CR_Q_NONE). A major triad WITH an extension keeps
 * Orchid's name (C7, CM7, C6, C9, C JAZZ: the figure already says "a chord", and "CM7" must stay the major 7th, never
 * "CM" + "7"); the power chord keeps "C5". Every screen names a chord through cr_chord_name.
 * Options > Chord Names (0.20): Orchid, the default, is all of the above, unchanged; Standard names what sounds
 * (crx_name_std), and where that has no name (both sevenths at once) Orchid's stands. */
static uint8_t crx_names;        /* Options > Chord Names: 0 Orchid, 1 Standard. Display only: no voice, no stored
                                  * event and no MIDI byte reads it */
void cr_chord_names_set(int standard) { crx_names = (uint8_t)(standard != 0); }
int cr_chord_names(void) { return crx_names; }

static void crx_app(char *d, const char *s, int max)   /* crx_cat without the space */
{
    int i = 0;
    while (d[i]) i++;
    while (*s && i < max - 1) d[i++] = *s++;
    d[i] = 0;
}

/* the figure of an extension set without both sevenths, as a lead sheet writes it (docs/CHORD-NAMES.md): 6, 6/9,
 * add9; 7, 9, 13 (a seventh with the 6th: the 13th, whether the 9th sounds with it or not), "M" before it for the
 * major seventh */
static void crx_figure(uint8_t ext, char *d, int max)
{
    d[0] = 0;
    if (ext & (CR_EXT_M7 | CR_EXT_MAJ7)) {
        if (ext & CR_EXT_MAJ7) crx_str(d, "M", max);
        crx_app(d, (ext & CR_EXT_6) ? "13" : (ext & CR_EXT_9) ? "9" : "7", max);
    } else if (ext & CR_EXT_6) {
        crx_str(d, (ext & CR_EXT_9) ? "6/9" : "6", max);
    } else if (ext & CR_EXT_9) {
        crx_str(d, "add9", max);
    }
}

/* Options > Chord Names > Standard (0.20; docs/CHORD-NAMES.md has the 128 cells and the reasons): the lead-sheet name
 * of the notes that sound, by the tables at the top (CR_QUALITY, CR_EXT_IV), not by the buttons' labels. The same
 * three strings as Orchid's (root at full size, qual at 0.58, sup the superscript); read in a row they are the name.
 * 1 = named; 0 = no name of its own (both sevenths at once): the caller keeps Orchid's */
static int crx_name_std(uint8_t quality, uint8_t ext, char qual[5], char sup[CR_SUP_MAX])
{
    char f[5];
    if ((ext & (CR_EXT_M7 | CR_EXT_MAJ7)) == (CR_EXT_M7 | CR_EXT_MAJ7)) return 0;
    crx_figure(ext, f, sizeof f);
    qual[0] = sup[0] = 0;
    switch (quality) {
    case CR_Q_MAJ:                   /* CM (the triad keeps its M: "C" alone is a single note), C6 C7 Cadd9 C9 C13 CM7 */
        if (!ext) crx_str(qual, "M", 5);
        crx_str(sup, f, CR_SUP_MAX);
        break;
    case CR_Q_MIN:                   /* Cm Cm6 Cm7 Cmadd9 Cm9 Cm13 CmM7 */
        crx_str(qual, "m", 5);
        crx_str(sup, f, CR_SUP_MAX);
        break;
    case CR_Q_AUG:                   /* C+ C+7 C+M7 C+9 */
        crx_str(qual, "+", 5);
        crx_str(sup, f, CR_SUP_MAX);
        break;
    case CR_Q_SUS:                   /* 0 5 7: Csus4, Csus4add9; a figure goes before it: C7sus4 C9sus4 C6sus4 */
        if (!ext || ext == CR_EXT_9) {
            crx_str(qual, "sus4", 5);
            crx_str(sup, f, CR_SUP_MAX);
        } else {
            crx_str(sup, f, CR_SUP_MAX);
            crx_app(sup, "sus4", CR_SUP_MAX);
        }
        break;
    case CR_Q_POW:                   /* 0 7: C5; the 9th makes it Csus2 (C7sus2 ..); without it there is no third to
                                      * name: C6no3 C7no3 */
        crx_figure((uint8_t)(ext & ~CR_EXT_9), f, sizeof f);
        if (!ext) crx_str(sup, "5", CR_SUP_MAX);
        else if (ext == CR_EXT_9) crx_str(qual, "sus2", 5);
        else {
            crx_str(sup, f, CR_SUP_MAX);
            crx_app(sup, (ext & CR_EXT_9) ? "sus2" : "no3", CR_SUP_MAX);
        }
        break;
    case CR_Q_MIN4:                  /* 0 3 5: the minor chord's name and "add4" (no fifth sounds: Orchid's secret
                                      * chord; the name does not say so) */
        crx_str(qual, "m", 5);
        crx_str(sup, f, CR_SUP_MAX);
        crx_app(sup, "add4", CR_SUP_MAX);
        break;
    case CR_Q_DIM:                   /* 0 3 6. The 6 key adds 9 semitones, the diminished seventh: Cdim7. The 7 key
                                      * adds 10, the minor seventh: half-diminished, Cm7b5 (Cm9b5, Cm13b5). The major
                                      * seventh: the triad's name and its figure, CdimM7 CdimM9 CdimM13 */
        if (ext & CR_EXT_M7) {
            crx_str(qual, "m", 5);
            crx_str(sup, f, CR_SUP_MAX);
            crx_app(sup, "b5", CR_SUP_MAX);
            break;
        }
        crx_str(qual, "dim", 5);
        if (ext & CR_EXT_MAJ7) crx_str(sup, f, CR_SUP_MAX);
        else {
            if (ext & CR_EXT_6) crx_str(sup, "7", CR_SUP_MAX);
            if (ext & CR_EXT_9) crx_app(sup, "add9", CR_SUP_MAX);
        }
        break;
    default:
        return 0;
    }
    return 1;
}

void cr_chord_name(uint8_t root_pc, uint8_t quality, uint8_t ext, char root[3], char qual[5], char sup[CR_SUP_MAX])
{
    crx_str(root, CR_NOTE_NAMES[root_pc % 12u], 3);
    sup[0] = 0;
    if (quality >= CR_Q_COUNT) quality = CR_Q_NONE;
    crx_str(qual, CR_QUAL_NAMES[quality], 5);
    if (quality == CR_Q_NONE) return;
    if (quality >= CR_Q_6_3) {       /* the 6-Dim scales' (0.36), in either naming: "C6/E" "Cm6/D#" (the chord's root, its
                                      * bass after the slash; the extension keys add notes, not to the name), "C#7b9" */
        if (quality == CR_Q_7B9) { crx_str(sup, "7b9", CR_SUP_MAX); return; }
        crx_str(root, CR_NOTE_NAMES[(root_pc % 12u + (quality == CR_Q_6_3 ? 8u : quality == CR_Q_M6_3 ? 9u : 5u)) % 12u], 3);
        crx_str(sup, "6/", CR_SUP_MAX);
        crx_app(sup, CR_NOTE_NAMES[root_pc % 12u], CR_SUP_MAX);
        return;
    }
    ext &= 15u;
    if (crx_names && crx_name_std(quality, ext, qual, sup)) return;
    crx_str(qual, CR_QUAL_NAMES[quality], 5);
    sup[0] = 0;
    if (quality == CR_Q_POW) crx_str(sup, "5", CR_SUP_MAX);
    else if (quality == CR_Q_MIN4) crx_str(sup, "add4", CR_SUP_MAX);
    if (quality == CR_Q_MAJ && !ext) crx_str(qual, "M", 5);
    if (crx_bits(ext) >= 4) { crx_cat(sup, "WTF", CR_SUP_MAX); return; }
    if (crx_bits(ext) == 3) { crx_cat(sup, "JAZZ", CR_SUP_MAX); return; }
    if (ext & CR_EXT_M7) crx_cat(sup, quality == CR_Q_SUS ? "m7" : "7", CR_SUP_MAX);
    if (ext & CR_EXT_MAJ7) crx_cat(sup, "M7", CR_SUP_MAX);
    if (ext & CR_EXT_6) crx_cat(sup, "6", CR_SUP_MAX);
    if (ext & CR_EXT_9) crx_cat(sup, "9", CR_SUP_MAX);
}

/* the chord's Roman numeral in the key (0.22, the KEY layer's Numerals; docs/CHORD-NAMES.md "The numeral"): the degree
 * of its root above the tonic (major 0 2 4 5 7 9 11, natural minor 0 2 3 5 7 8 10 = I .. VII); a root between two
 * degrees is the neighbour with an accidental (major bII bIII #IV bVI bVII, minor bII #III #IV #VI #VII); lower case
 * with a minor third (MIN, DIM, MIN + SUS), upper case otherwise (a single note too); DIM ends in the degree sign
 * (Latin-1 0xB0), the augmented chord in "+". No extensions: the name beside it has them. Display only.
 * The same degree spelt as movable-do solfege (Numerals = Solfege, GitHub issue 15): DO RE MI FA SOL LA TI for I .. VII
 * from the tonic in either scale (A minor's C is MI as it is III), the accidental, the case and the marks the
 * numeral's: "SOL", "re", "ti\260", "#FA", "bTI" (docs/MANUAL.md "Numerals") */
static void crx_degree(uint8_t root_pc, uint8_t quality, uint8_t tonic, uint8_t scale, const char *const nm[7],
                       char buf[CR_NUMERAL_MAX])
{
    /* per semitone above the tonic: the degree 0..6 | 0x10 flat | 0x20 sharp */
    static const uint8_t DEG[2][12] = {
        { 0, 0x11, 1, 0x12, 2, 3, 0x23, 4, 0x15, 5, 0x16, 6 },
        { 0, 0x11, 1, 2, 0x22, 3, 0x23, 4, 5, 0x25, 6, 0x26 } };
    uint8_t d;
    const char *n;
    int i = 0, low;
    /* the 6-Dim scales' inversions read as the 6th chord they are (C6/E: I), 7b9 as a major chord on its bass */
    if (quality >= CR_Q_6_3 && quality < CR_Q_COUNT) {
        root_pc = (uint8_t)(root_pc % 12u + 12u - (quality == CR_Q_6_3 ? 4u : quality == CR_Q_M6_3 ? 3u
                                                                     : quality == CR_Q_7B9 ? 0u : 7u));
        quality = quality == CR_Q_M6_3 || quality == CR_Q_M6_5 ? CR_Q_MIN : CR_Q_MAJ;
    }
    d = DEG[cr_scale_minor(scale)][(root_pc % 12u + 12u - tonic % 12u) % 12u];
    n = nm[d & 7u];
    low = quality == CR_Q_MIN || quality == CR_Q_DIM || quality == CR_Q_MIN4;
    if (d & 0x30u) buf[i++] = d & 0x10u ? 'b' : '#';
    while (*n) buf[i++] = (char)(low ? *n++ | 0x20 : *n++);
    if (quality == CR_Q_DIM) buf[i++] = (char)0xB0;
    else if (quality == CR_Q_AUG) buf[i++] = '+';
    buf[i] = 0;
}
void cr_chord_numeral(uint8_t root_pc, uint8_t quality, uint8_t tonic, uint8_t scale, char buf[CR_NUMERAL_MAX])
{
    static const char *const RN[7] = { "I", "II", "III", "IV", "V", "VI", "VII" };
    crx_degree(root_pc, quality, tonic, scale, RN, buf);
}
void cr_chord_solfege(uint8_t root_pc, uint8_t quality, uint8_t tonic, uint8_t scale, char buf[CR_NUMERAL_MAX])
{
    static const char *const SO[7] = { "DO", "RE", "MI", "FA", "SOL", "LA", "TI" };
    crx_degree(root_pc, quality, tonic, scale, SO, buf);
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
        ci->transpose = v->loop ? c->transpose : v->transpose;
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
    ci->secret = ci->quality >= CR_Q_AUG && ci->quality <= CR_Q_MIN4;
    cr_chord_name(ci->root_pc, ci->quality, ci->ext, ci->root, ci->qual, ci->sup);
}

uint16_t cr_scale_mask(const cr_t *c)
{
    return c->key_on ? cr_scale_bits(c->tonic, c->scale) : 0;
}
uint16_t cr_scale_bits(uint8_t tonic, uint8_t scale)
{
    uint32_t m = CR_SCALE_MASK[scale < CR_SCALE_COUNT ? scale : 0], t = tonic % 12u;
    return (uint16_t)(((m << t) | (m >> (12u - t))) & 0xFFFu);
}
const char *cr_scale_name(uint8_t scale) { return CR_SCALE_NAMES[scale < CR_SCALE_COUNT ? scale : 0]; }
int cr_scale_minor(uint8_t scale) { return scale < CR_SCALE_COUNT && ((CR_SCALE_MINORS >> scale) & 1u); }

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

/* the part shown: the chord's performance while it runs on the shown voice, else the bass's (the bass's step is matched
 * to the chord's notes by its name: crx_pf_notes moves the chord by octaves) */
int cr_perform_shown(const cr_t *c)
{
    const cr_voice_t *v;
    if (c->disp_vi == CR_NONE || !c->perform_on) return -1;
    v = &c->v[c->disp_vi];
    if (!v->used) return -1;
    if (crx_pf_on(c, CR_PF_CHORD) && v->pf[CR_PF_CHORD].own.live) return CR_PF_CHORD;
    if (crx_pf_on(c, CR_PF_BASS) && v->pf[CR_PF_BASS].own.live) return CR_PF_BASS;
    return -1;
}

int cr_perform_pos(const cr_t *c, uint8_t *note)
{
    const cr_voice_t *v;
    const cr_pf_t *f;
    int i, part = cr_perform_shown(c);
    if (part < 0) return -1;
    v = &c->v[c->disp_vi];
    f = &v->pf[part];
    if (!f->has_last) return -1;
    if (note) *note = f->last_note;
    for (i = 0; i < v->nnotes; i++) if (v->notes[i] == f->last_note) return i;
    for (i = 0; i < v->nnotes; i++)
        if ((f->last_note + 120 - v->notes[i]) % 12 == 0) return i;
    return -1;
}

/* the shown voice is being arpeggiated: its clock runs (Arp / Pattern; a strum, a slop and a harp run once and
 * end). True from the press (before the first step, Retrig off) to the voice's end: keys up without a latch,
 * Perform off, panic, its loop stopped */
int cr_perform_stepping(const cr_t *c)
{
    int part = cr_perform_shown(c);
    return part >= 0 && c->v[c->disp_vi].pf[part].clk.on;
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
