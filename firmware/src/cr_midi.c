/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: MIDI in, the pure part (no firmware state; tests/cr_midi_test.c). cr_out.c includes it and does
 * the plumbing (docs/INTEGRATION.md section 2):
 *   crm_clock_*   the clock-in tempo follower: 24 PPQN pulses (ms stamps) -> BPM 20..300, averaged over four beats
 *   crm_map       a channel message on the CHORD / BASS channels -> what ChoralRoot does with it
 *   crm_cc_*      the CCs on the sounds' parameters (Options > MIDI CC; docs/MANUAL.md section 15 "MIDI in"): which
 *                 CCs, the editor's blocks, the 0..127 scaling both ways, the echo guard (HISTORY 3y269) */
#include <stdint.h>

/* ---------------------------------------------------- the clock follower --- */
/* (3y283, GitHub issue 39) four beats averaged, not one: a DAW that sends its clock once an audio buffer stamps the
 * pulses up to a buffer late (21 ms at 1024 samples), and over one beat that swung the tempo 117..121 and reset the
 * FM-1's clock at every change. The last beat alone still decides a jump: when it is more than CRM_JUMP % away from the
 * four beats', the window starts again from it (a tempo jump settles within about a beat, as before) */
#define CRM_WIN 96u                      /* pulses averaged: four beats */
#define CRM_BEAT 24u
#define CRM_JUMP 6u                      /* % (a buffer's stamps over a beat: about 4 %) */
typedef struct {
    uint32_t t[CRM_WIN + 1u];            /* the last pulses' stamps (ms), a ring */
    uint32_t n;                          /* pulses in the window (0..CRM_WIN + 1) */
    uint32_t w;                          /* the next write */
    uint16_t bpm;                        /* the tempo reported (0: none yet) */
    uint16_t bpm10;                      /* the window's last measure (tenths of a BPM) */
} crm_clock_t;

static void crm_clock_reset(crm_clock_t *c)
{
    uint32_t i;
    for (i = 0; i <= CRM_WIN; i++)
        c->t[i] = 0;
    c->n = c->w = 0;
    c->bpm = c->bpm10 = 0;
}

static uint32_t crm_back(const crm_clock_t *c, uint32_t j)   /* the stamp j pulses before the newest */
{
    return c->t[(c->w + 2u * (CRM_WIN + 1u) - 1u - j) % (CRM_WIN + 1u)];
}

/* one 0xF8 at ms. Returns the tempo to apply (20..300), or 0: no change. A gap longer than a 20 BPM pulse restarts the
 * window; every pulse counts, however close to the one before (until 3y283 one < 2 ms after was ignored as a
 * duplicate: a burst's pulses are real, and one clock on two inputs is cr_out.c's to count once) */
static int crm_clock_pulse(crm_clock_t *c, uint32_t ms)
{
    uint32_t k, b10, g, j, so = 0, sn = 0, oldest, s1;
    if (c->n && ms - crm_back(c, 0) > 150u)   /* (a 20 BPM pulse is 125 ms): a stop, a cable: start again */
        c->n = 0;
    c->t[c->w] = ms;
    c->w = (c->w + 1u) % (CRM_WIN + 1u);
    if (c->n <= CRM_WIN)
        c->n++;
    if (c->n < 7u)                          /* 6 intervals (a quarter beat) before the first tempo */
        return 0;
    if (c->n > CRM_BEAT + 1u) {             /* the last beat against the window: a jump starts the window again */
        uint32_t a, b;
        s1 = ms - crm_back(c, CRM_BEAT);
        k = c->n - 1u;
        a = CRM_BEAT * (ms - crm_back(c, k));
        b = k * s1;
        if ((a > b ? a - b : b - a) / CRM_JUMP > b / 100u)
            c->n = CRM_BEAT + 1u;
    }
    /* the window's first and last quarter, each averaged: its ends' stamps without most of their jitter */
    g = c->n / 4u;
    oldest = crm_back(c, c->n - 1u);
    for (j = 0; j < g; j++) {
        so += crm_back(c, c->n - 1u - j) - oldest;
        sn += crm_back(c, j) - oldest;
    }
    if (sn <= so)
        return 0;
    b10 = (600000u * (c->n - g) / 24u * g + (sn - so) / 2u) / (sn - so);   /* tenths of a BPM */
    b10 = b10 < 200u ? 200u : b10 > 3000u ? 3000u : b10;
    c->bpm10 = (uint16_t)b10;
    k = (b10 + 5u) / 10u;
    if (k == c->bpm)
        return 0;
    if (c->bpm && (b10 > c->bpm * 10u ? b10 - c->bpm * 10u : c->bpm * 10u - b10) < 8u)
        return 0;                           /* within the stamps' jitter of the tempo applied: no clock reset */
    c->bpm = (uint16_t)k;
    return (int)k;
}

/* ------------------------------------------------- channel messages --- */
enum {
    CRM_NONE,        /* ignored */
    CRM_FORWARD,     /* to Felucca's MIDI in (midi_control.c) as the part's channel: notes, pedal, bend, CC 123 */
    CRM_LEVEL,       /* v: the part's LEVEL 0..127 */
    CRM_SEND_REV, CRM_SEND_CHO, CRM_SEND_DLY,   /* v: the part's send 0..127 */
    CRM_PROGRAM      /* v: the sound list position (PRESETS / ALGORITHM) */
};
#define CRM_CC 0x80u     /* kind CRM_CC | cc: a CC on a sound's parameter (crm_cc_eng), v its value 0..127 */
typedef struct { uint8_t kind, part; uint8_t v; } crm_act_t;

/* ------------------------------------------- CCs on the sounds' parameters --- */
/* Options > MIDI CC In (3y269): these CCs set a parameter of the part's sound (cr_edit.c ce_cc_in), the others are
 * as before. 10 Pan, 92 the Drive send, 71..77 and 79 the common ones (cutoff, resonance, the amp envelope, LFO 1),
 * then the editor's groups, a block each (the group's lanes in SELECT order, four CCs a lane = KNOB 1..4; what does
 * not fit the block has no CC): OSC 16..31, FILT 32..47, ENV 48..63, LFO 102..113, MOD 80..90 */
static const uint8_t CRM_BLK[5][2] = {{16, 16}, {32, 16}, {48, 16}, {102, 12}, {80, 11}};   /* first CC, CCs */
#define CRM_NCC 81u      /* the CCs crm_cc_eng takes (crm_cc_slot: 0 .. CRM_NCC - 1) */
static int crm_cc_eng(uint32_t cc)
{
    return cc == 10u || (cc >= 16u && cc <= 63u) || (cc >= 71u && cc <= 79u && cc != 78u) || (cc >= 80u && cc <= 90u) ||
           cc == 92u || (cc >= 102u && cc <= 113u);
}
/* a CC of crm_cc_eng -> 0 .. CRM_NCC - 1 (its place in cr_out.c's pending values), -1 another */
static int32_t crm_cc_slot(uint32_t cc)
{
    if (!crm_cc_eng(cc))
        return -1;
    return cc == 10u ? 0 : cc <= 63u ? (int32_t)cc - 15 : cc <= 77u ? (int32_t)cc - 22 : cc == 79u ? 56
         : cc <= 90u ? (int32_t)cc - 23 : cc == 92u ? 68 : (int32_t)cc - 33;
}
/* an editor block's CC -> its group (0 OSC .. 4 MOD; -1: not a block's), *lane the lane 0.. in the group, *knob 0..3 */
static int32_t crm_cc_block(uint32_t cc, uint32_t *lane, uint32_t *knob)
{
    uint32_t g;
    for (g = 0; g < 5u; g++)
        if (cc >= CRM_BLK[g][0] && cc < (uint32_t)CRM_BLK[g][0] + CRM_BLK[g][1]) {
            *lane = (cc - CRM_BLK[g][0]) / 4u;
            *knob = (cc - CRM_BLK[g][0]) % 4u;
            return (int32_t)g;
        }
    return -1;
}
/* .. and back: group g's lane, knob -> its CC, 0 none (past the block) */
static uint32_t crm_cc_of(uint32_t g, uint32_t lane, uint32_t knob)
{
    uint32_t i = lane * 4u + (knob & 3u);
    return g < 5u && i < CRM_BLK[g][1] ? CRM_BLK[g][0] + i : 0u;
}
/* 0..127 -> lo..hi: linear, rounded; list (an enum's names): equal slices. So value -> CC -> value is the value again
 * for a range of 128 steps or fewer (crm_cc_out rounds to the nearest CC, a list to its slice's middle) */
static int32_t crm_cc_val(uint32_t cc, int32_t lo, int32_t hi, int list)
{
    cc &= 0x7Fu;
    if (hi <= lo)
        return lo;
    if (list)
        return lo + (int32_t)(cc * (uint32_t)(hi - lo + 1) / 128u);
    return lo + (int32_t)((cc * (uint32_t)(hi - lo) + 63u) / 127u);
}
static uint32_t crm_cc_out(int32_t v, int32_t lo, int32_t hi, int list)
{
    uint32_t r, n;
    if (hi <= lo)
        return 0;
    v = v < lo ? lo : v > hi ? hi : v;
    r = (uint32_t)(v - lo);
    n = (uint32_t)(hi - lo);
    if (list) {                                 /* the middle of the slice: its CCs are ceil(r * 128 / (n + 1)) .. */
        uint32_t a = (r * 128u + n) / (n + 1u), b = ((r + 1u) * 128u + n) / (n + 1u);
        return a < b ? (a + b - 1u) / 2u : a > 127u ? 127u : a;   /* (more than 128 names: a slice may be empty) */
    }
    return (r * 127u + n / 2u) / n;
}
/* the echo guard: the CCs the FM-1 sent last (channel, CC, value, when), so that a DAW echoing them back does not move
 * the parameter again (a value of more than 128 steps would jitter on its CC's rounding, an old echo would step it
 * back). An incoming CC equal to one sent on its channel within CRM_ECHO_MS is ignored */
#define CRM_ECHO_N 8u
#define CRM_ECHO_MS 1000u
typedef struct { uint8_t ch, cc, v, t; } crm_echo_t;     /* ch + 1 (0: an unused entry), the CC, its value, ms / 8 */
typedef struct { crm_echo_t e[CRM_ECHO_N]; uint8_t w; } crm_echoes_t;
static void crm_echo_note(crm_echoes_t *x, uint32_t ch, uint32_t cc, uint32_t v, uint32_t ms)
{
    crm_echo_t *e = &x->e[x->w++ % CRM_ECHO_N];
    e->ch = (uint8_t)((ch & 15u) + 1u);
    e->cc = (uint8_t)(cc & 0x7Fu);
    e->v = (uint8_t)(v & 0x7Fu);
    e->t = (uint8_t)(ms >> 3);
}
static int crm_echo_hit(const crm_echoes_t *x, uint32_t ch, uint32_t cc, uint32_t v, uint32_t ms)
{
    uint32_t i;
    for (i = 0; i < CRM_ECHO_N; i++)
        if (x->e[i].ch == (ch & 15u) + 1u && x->e[i].cc == (cc & 0x7Fu) && x->e[i].v == (v & 0x7Fu) &&
            (uint8_t)((uint8_t)(ms >> 3) - x->e[i].t) < CRM_ECHO_MS / 8u)   /* (the stamp wraps at 2 s: within 1 s) */
            return 1;
    return 0;
}

/* status (0x80..0xEF), d1, d2; the CHORD and BASS channels (0..15, en 0: Off). Up to two actions (one channel can
 * be both: part 0 and part 1 then). cc_in: Options > MIDI CC In (the CCs of crm_cc_eng: CRM_CC; off: forwarded as
 * before). Returns the number written to a[]. */
static int crm_map(uint32_t status, uint32_t d1, uint32_t d2, const uint8_t en[2], const uint8_t ch[2],
                   uint32_t cc_in, crm_act_t a[2])
{
    uint32_t st = status & 0xF0u, c = status & 0x0Fu, p;
    int n = 0;
    for (p = 0; p < 2u; p++) {
        uint8_t k = CRM_NONE;
        if (!en[p] || (ch[p] & 15u) != c)
            continue;
        if (st == 0xB0u)
            k = d1 == 7u ? CRM_LEVEL : d1 == 91u ? CRM_SEND_REV : d1 == 93u ? CRM_SEND_CHO
              : d1 == 94u ? CRM_SEND_DLY : cc_in && crm_cc_eng(d1) ? (uint8_t)(CRM_CC | d1) : CRM_FORWARD;
        else if (st == 0xC0u)
            k = CRM_PROGRAM;
        else if (st == 0x80u || st == 0x90u || st == 0xA0u || st == 0xD0u || st == 0xE0u)
            k = CRM_FORWARD;
        if (k == CRM_NONE)
            continue;
        a[n].kind = k;
        a[n].part = (uint8_t)p;
        a[n].v = (uint8_t)((k == CRM_PROGRAM ? d1 : d2) & 0x7Fu);
        n++;
    }
    return n;
}

/* --------------------------------------------------------- MIDI Keys --- */
/* Options > MIDI Keys (docs/CHORD-LAYOUT.md "MIDI in"): on that channel (keys_ch 1 .. 16, 0 Off) a note n 53 .. 79
 * presses the FM-1's key n - 53 (F3 .. G5) as the keybed does, through the active layout; 3y289: a note 36 .. 44 (a
 * pad controller's first pads) presses the key n - 36 too, F3 .. C#4, the chord block. A message on the channel
 * -> *ev = key | vel << 5 | down << 12 (CRM_KEY_ALLUP as the key: every key MIDI holds up: CC 120, CC 123). Returns
 * 1 with an event, 0 for a message on the channel that does nothing (a note out of range, another message), -1: not
 * the keys' channel */
#define CRM_KEY_ALLUP 31u
static int crm_key(uint32_t status, uint32_t d1, uint32_t d2, uint32_t keys_ch, uint32_t *ev)
{
    uint32_t st = status & 0xF0u;
    if (!keys_ch || (status & 0x0Fu) != keys_ch - 1u)
        return -1;
    if (st == 0xB0u && (d1 == 120u || d1 == 123u)) {
        *ev = CRM_KEY_ALLUP;
        return 1;
    }
    if ((st != 0x90u && st != 0x80u) || d1 > 79u || (d1 < 53u && (d1 < 36u || d1 > 44u)))
        return 0;
    *ev = (d1 - (d1 < 53u ? 36u : 53u)) | (d2 & 0x7Fu) << 5 | (st == 0x90u && d2 ? 1u : 0u) << 12;
    return 1;
}

/* Options > MIDI Roots (3y289): on that channel (roots_ch 1 .. 16, 0 Off) every note is a root at its own pitch, as
 * the root keys play one (cr_ui.c cu_mroot). *ev = CRM_ROOT | note | vel << 7 | down << 14; CC 120 / 123:
 * CRM_ROOT_ALLUP. The same returns as crm_key */
#define CRM_ROOT 0x8000u
#define CRM_ROOT_ALLUP (CRM_ROOT | 1u << 14 | 127u)   /* (down with velocity 0: no note's) */
static int crm_root(uint32_t status, uint32_t d1, uint32_t d2, uint32_t roots_ch, uint32_t *ev)
{
    uint32_t st = status & 0xF0u;
    if (!roots_ch || (status & 0x0Fu) != roots_ch - 1u)
        return -1;
    if (st == 0xB0u && (d1 == 120u || d1 == 123u)) {
        *ev = CRM_ROOT_ALLUP;
        return 1;
    }
    if (st != 0x90u && st != 0x80u)
        return 0;
    *ev = CRM_ROOT | d1 | d2 << 7 | (st == 0x90u && d2 ? 1u : 0u) << 14;
    return 1;
}

/* ----------------------------------------------- start / stop (Clock In) --- */
/* 0xFA start / 0xFB continue / 0xFC stop against the loop: playing (1/0), stopped with a loop (1/0), a take in
 * progress (1/0). Returns how many LOOP taps (cr_loop_play toggles) to apply: 0, 1, or 2 (FA while playing: stop and
 * start again, from the top). */
static int crm_transport(uint32_t st, int playing, int stopped, int capturing)
{
    if (capturing)
        return 0;                           /* (a take in progress is the player's) */
    if (st == 0xFCu)
        return playing ? 1 : 0;
    if (st == 0xFAu)
        return playing ? 2 : stopped ? 1 : 0;
    if (st == 0xFBu)
        return !playing && stopped ? 1 : 0;
    return 0;
}
