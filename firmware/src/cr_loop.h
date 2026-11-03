/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the semantic looper (PLAN.md section 8 M6, docs/INTEGRATION.md section 8, docs/LOOPER.md).
 * Portable C99, integer only, no allocation, no libc (stdint.h + cr_engine.h). One cr_loop_t the caller allocates;
 * it runs where the engine runs (the audio ISR): cr_loop_tick after cr_tick, the gesture hook from cr_out_t.
 *
 * What it records: chord gestures as the engine resolved them (absolute root, quality, extension mask, velocity)
 * with their on time and duration in ticks of a beat clock (CRL_PPQN per quarter note at the engine's BPM), in
 * layers. What it plays: those chords back through cr_loop_event (loop voices), so voicing, performance and bass
 * apply live, and BPM changes the loop's speed, never its pitch. */
#ifndef CR_LOOP_H
#define CR_LOOP_H

#include <stdint.h>
#include "cr_engine.h"

#define CRL_PPQN 96u             /* ticks per quarter note: 1/8T = 32, 1/16T = 16, 1/32 = 12 */
#ifndef CRL_MAX_EV
#define CRL_MAX_EV 512u          /* events per loop (all layers): 8 bytes each in RAM, 7 in flash */
#endif
#define CRL_MAX_LAYERS 32u       /* the first recording + 31 overdubs */
#define CRL_OPEN 16u             /* gestures tracked while open (one per held root is plenty) */
#define CRL_MIN_LEN 24u          /* a free loop is at least a 1/16 at the tempo it was made */
#define CRL_MAX_LEN 65535u       /* ticks (u16 event times): 170 bars of 4/4 */
#define CRL_SLOTS 10u
#define CRL_NSYNC 6u             /* Free, 1, 2, 4, 8, 16 bars */
#define CRL_NQUANT 7u            /* none, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 */
#define CRL_NEXT_SLOT(k) (((k) + 1u) % CRL_SLOTS)   /* Advance: the slot after k (10 wraps to 1) */
#define CRL_NRATE 11u            /* the loop's Rate: x1/16 x1/12 x1/8 x1/6 x1/4 x1/2 x3/4 x1 x1.5 x2 x4
                                  * (cr_loop_set_rate; the four slowest since 2026-10-11: at x1/16 a step is a bar) */
#define CRL_RATE_1 7u            /* .. x1, the default. The session stores the Rate as steps from here (-7 .. 3), so
                                  * a rate added at either end moves no stored value */
#define CRL_STEP 24u             /* step entry and the loop editor: a step is a sixteenth note of the loop's own time
                                  * (16 steps a bar of 4/4 at Rate x1; the Rate scales it with the loop). Until
                                  * 2026-10-12 the step was the Quantize value (a quarter note when none) */
#define CRL_STEPS 64u            /* a pattern's steps at most: step entry, the editor's Steps; a note's length in steps */
/* A STEP's properties (the loop editor's Trig page; docs/LOOPER.md "Ratchet, condition, voicing"): they belong to the
 * step, i.e. to every event that starts in it (both sides), for the loop's first CRL_STEPS steps; an empty step has
 * none. crl_data_t.st[step][0] = ratchet (bits 0-1: 0 off, 1 .. 3 = x2 .. x4) | condition << 2 (CRL_C_*);
 * st[step][1] = the voicing (bits 0-4: 0 Follow (the live voicing), 1 .. 25 = a fixed rotation -12 .. +12) | the
 * chance << 5 (0 .. 7: CRL_CHANCE_PCT, 0 = 100 %). A step plays on a pass when its condition is met AND its chance
 * roll passes: one roll per step per pass (both sides, every hit of a ratchet) */
enum { CRL_C_ALWAYS, CRL_C_1_2, CRL_C_2_2, CRL_C_1_3, CRL_C_2_3, CRL_C_3_3, CRL_C_1_4, CRL_C_2_4, CRL_C_3_4, CRL_C_4_4,
       CRL_C_FIRST, CRL_C_NFIRST, CRL_NCOND };
#define CRL_NRATCH 4u            /* off, x2, x3, x4 */
#define CRL_NVOIC 26u            /* Follow, -12 .. +12 */
#define CRL_NCHANCE 8u           /* 100 90 75 60 50 40 25 10 % */
#define CRL_CHANCE_PCT(k) ((uint8_t)("\144\132\113\074\062\050\031\012"[(k) & 7u]))
#define CRL_FRAC 6u              /* a note shorter than a step: 1/4, 1/2, 3/4 of it (CRL_FRAC, 2x, 3x ticks) */

enum { CRL_EMPTY, CRL_STOPPED, CRL_PLAYING };                  /* the loop */
enum { CRL_CAP_NONE,                                            /* capture */
       CRL_CAP_ARMED,       /* Free, a new loop: the first chord starts it (REC lit) */
       CRL_CAP_COUNTIN,     /* synced: the one-bar count-in (REC blinks, the click counts) */
       CRL_CAP_REC,         /* recording the first layer (REC blinks) */
       CRL_CAP_OD_ARMED,    /* playing, overdub armed: the next chord opens a layer (REC lit) */
       CRL_CAP_OD,          /* overdubbing a layer (REC blinks); Replace: the same with lp->rep */
       CRL_CAP_STEP };      /* step entry (docs/LOOPER-MODES.md): the loop stopped, chords written at a cursor (REC lit) */
enum { CRL_MODE_OVERWRITE, CRL_MODE_ADVANCE, CRL_MODE_OVERDUB, CRL_MODE_REPLACE, CRL_MODE_STEP, CRL_NMODE };
#define CRL_HID 0x80u            /* crl_ev_t.layer: hidden by a Replace layer (| that layer's index); RAM only */
#define CRL_LAYER(e) ((e)->layer & CRL_HID ? 0xFFu : (e)->layer)   /* a live event's layer (0xFF: hidden) */
enum { CRL_SIG_44, CRL_SIG_34, CRL_SIG_68, CRL_NSIG };

typedef struct { uint16_t t, dur; uint8_t root, vel, qx, layer; } crl_ev_t;   /* qx = quality << 4 | ext; layer: its
                                                                                 * layer, or CRL_HID | the Replace layer
                                                                                 * that hid it (undoing that layer brings
                                                                                 * it back; the flash record has only
                                                                                 * the live events) */
/* The part of a single note (quality 0: it has no extensions, so its ext nibble is free): CRL_X_BASS = played on the
 * bass side with Single = Split Bass: it plays on the bass part (cr_loop_event's CR_GX_BASS), whatever Single says at
 * playback. Decided when it is recorded, kept in qx in RAM and in the record: nothing a layer does (Replace's hide and
 * cut, undo, quantize, Step's commit) touches qx. */
#define CRL_X_BASS 1u
#define CRL_EV_BASS(e) (((e)->qx & 0xF0u) == 0u && ((e)->qx & CRL_X_BASS))

typedef struct {                 /* a loop's content: what a slot stores */
    uint32_t len;                /* ticks, 0 = empty */
    uint16_t nev;
    uint8_t nlayers, sig;        /* sig: the time signature it was made in (CRL_SIG_*) */
    uint8_t st[CRL_STEPS][2];    /* the steps' properties (above); all 0: none (128 bytes; before ev: crl_data_size) */
    crl_ev_t ev[CRL_MAX_EV];     /* in layer order (a layer's events follow the previous layer's) */
} crl_data_t;

typedef struct {
    /* settings (the UI's, posted) */
    uint8_t sync, quant, count_in, level;      /* 0..5, 0..6, 0/1, 0..100 (loop velocity %) */
    uint8_t sig, metro, metro_vol;             /* CRL_SIG_*, click on, click level 0..100 */
    uint8_t mode;                              /* CRL_MODE_*: what REC does with no capture (Advance: the UI's slot jump,
                                                * then as Overwrite) */
    /* the loop */
    crl_data_t d;
    uint8_t state, cap, full;
    uint16_t layer_ev0;                        /* the capture layer's first event */
    /* the clocks, from the ms clock: rt, ticks at the engine's BPM (the tempo's: the count-in, the click); t, the
     * LOOP's ticks: rt scaled by the Rate (crl_tick1: rate 48ths of a loop tick each tempo tick, rfrac the
     * remainder), what every time of the loop's content is in (events, takes, the cycle, the voices' ends) */
    uint32_t t, acc, last_ms;
    uint32_t rt;
    uint8_t clk_on;
    /* MIDI Clock In (cr_loop_ext_pulse): ext_on, pulses are coming; ext_rt, the tempo tick the last pulse is at
     * (CRL_PPQN / 24 = 4 a pulse). rt then never runs more than a pulse's 3 ticks past it and catches up to it at once:
     * the master's clock, not the FM-1's, keeps the loop's time (HISTORY 3y224) */
    uint8_t ext_on;
    uint32_t ext_rt;
    uint8_t rate, rate_want, rfrac;            /* CRL_NRATE: in force; asked for (taken at the loop's start); 0..47 */
    uint32_t rrec0, rcyc0;                     /* the take's / the cycle's start in tempo ticks (the click's grid) */
    uint32_t cyc0, scan;                       /* the current cycle's start (abs ticks); next position to play */
    uint32_t rec0, rec_len, cin0, od0, met0;
    struct { uint32_t gid, t0; int16_t root; uint8_t used, q, ext, vel, rep; } open[CRL_OPEN];   /* held gestures
                                                * (rep: opened while replacing; used 2: its key is up and it sounds on,
                                                * latched, and the editor has written it: CR_G_KEYUP) */
    /* the loop voices. ev: the event it plays; used: 1 sounding (off: its end), 2 a ratchet waiting for its next hit
     * (off: when); hits: the ratchet's hits still to come, slice ticks apart, each gate long but the LAST, which is
     * `last` long (a plain note: its only hit, the note's length; a ratchet on a note longer than a step: the rest of
     * the note, so the last hit holds to the note's end), with the chord kept here (the event may be gone by then:
     * an edit, a slot switch); voic: the step's voicing (CR_VOIC_FOLLOW) */
    struct { uint32_t off; uint16_t ev, last; uint8_t used, hits, slice, gate, root, qx, vel; int8_t voic; } pv[CR_MAX_LOOPV];
    uint8_t pass12, pass_n;                    /* the pass of the loop since it started playing, mod 12 (the A:B
                                                * conditions: 2, 3 and 4 divide it); 0 on the first, then 1 */
    uint32_t rnd;                              /* the chance rolls' state: a new value each pass (xorshift32) */
    uint16_t lev0[CRL_MAX_LAYERS];             /* each layer's first event (a hidden event's layer, back on undo) */
    uint8_t ow;                                /* Overwrite armed over a loop: it is cleared when the take starts */
    uint8_t rep, rep_n;                        /* the overdub replaces (Replace); gestures of it held */
    uint32_t rep0;                             /* the cycle position the held span began at (ticks) */
    uint16_t step, step_end;                   /* step entry: the cursor (Free: CRL_STEPS = past the last step: the
                                                * pattern is full); the steps used (Free: the length) */
    uint8_t step_g, step_w;                    /* the grid (ticks a step: CRL_STEP); a chord written at the cursor */
    uint8_t step_no;                           /* + 1 whenever a chord was refused (Free: a 65th step) */
    /* the loop editor (cr_loop_ed_*; docs/LOOPER.md "The loop editor"): on; its grid (ticks a step: CRL_STEP); the
     * cursor's step: its first tick; ed_n: the length, in steps, of the notes the keys now down write (the length the
     * step's notes had; 1 for an empty step); the sides (bit 0 chord, bit 1 bass) already replaced by the keys now
     * down; the loop's extent with the steps Loop length hid */
    uint8_t ed, ed_g, ed_n, ed_w;
    uint8_t ed_nf;                             /* .. ed_n is a part of a step: 1 .. 3 quarters (0: whole steps) */
    uint8_t ed_flat;                           /* .. an edit was made: the loop is one layer */
    uint32_t ed_t0, ed_len;
    uint8_t pre, pre_end;                      /* the step preview (cr_loop_ed_preview): bit lv = loop voice lv sounds
                                                * the cursor's step, struck on a cursor move while the loop is stopped
                                                * (not a hit of the loop: not counted, not its "now playing"); pre_end:
                                                * a key was played by hand (cr_loop_gesture) or the step cleared: the
                                                * preview ends at the next cr_loop_tick (never inside the engine's
                                                * gesture hook) */
    uint32_t step_len;                         /* a set Length: the ticks the cursor wraps at (whole bars, CRL_STEPS
                                                * steps at most; 0: Free) */
    uint16_t gen;                              /* + 1 whenever the events change (the UI's timeline cache) */
    const crl_data_t *next;                    /* a slot switch waiting for the end of the cycle (0: empty) */
    uint8_t next_on, dirty;                    /* a switch is queued; the content changed since loaded / saved */
    uint8_t loads;                             /* + 1 whenever a slot's data replaces the loop: the UI tells a queued
                                                * switch taken from one cancelled (a stop, a clear, a panic, or an edit
                                                * of the loop still playing: an overdub armed, an undo) */
    /* out: the click (the audio side takes it), diagnostics */
    uint8_t click;                             /* 0 none, 1 beat, 2 the bar's first beat */
    uint8_t hit_on, hit_root, hit_qx;          /* the chord the loop struck last since it started playing (crl_hit: what
                                                * sounded, so a step its condition or chance kept silent is not it);
                                                * hit_on 0: none yet. The loop editor's "now playing" name */
    uint32_t played;                           /* events played (diagnostics) */
} cr_loop_t;

void cr_loop_init(cr_loop_t *lp);
void cr_loop_seed(cr_loop_t *lp, uint32_t seed);   /* the steps' chance rolls: the same seed and the same
                                                * timing give the same passes (the host tests) */
void cr_loop_tick(cr_loop_t *lp, cr_t *c, uint32_t now_ms);
void cr_loop_ext_pulse(cr_loop_t *lp);         /* a 0xF8 in (Clock In): the tempo ticks lock to the pulses */
void cr_loop_ext_off(cr_loop_t *lp);           /* no pulses (Clock In off, a gap, FA / FB: the next pulse re-anchors) */
/* cr_out_t.gesture's body */
void cr_loop_gesture(cr_loop_t *lp, uint32_t gid, int16_t root, uint8_t q, uint8_t ext, uint8_t vel, int on);

/* transport (PLAN.md section 3). Each returns what it did (CRL_DID_*) for the UI's message */
enum { CRL_DID_NOTHING, CRL_DID_ARM, CRL_DID_COUNTIN, CRL_DID_REC, CRL_DID_CANCEL, CRL_DID_COMMIT, CRL_DID_OD_ARM,
       CRL_DID_OD_END, CRL_DID_PLAY, CRL_DID_STOP, CRL_DID_UNDO, CRL_DID_CLEAR, CRL_DID_EMPTY_TAKE, CRL_DID_REP_ARM,
       CRL_DID_STEP, CRL_DID_STEP_DONE };
int  cr_loop_rec(cr_loop_t *lp, cr_t *c);      /* REC tap */
int  cr_loop_play(cr_loop_t *lp, cr_t *c);     /* LOOP tap */
int  cr_loop_undo(cr_loop_t *lp, cr_t *c);     /* REC hold, F#4: the last layer (an overdub in progress first) */
int  cr_loop_clear(cr_loop_t *lp, cr_t *c);    /* D#4 held 1 s */
enum { CRL_STEP_KEYSUP, CRL_STEP_REST, CRL_STEP_BACK };
void cr_loop_step(cr_loop_t *lp, int op);      /* step entry: every key up (advances after a chord), OCT+, OCT- */
uint32_t cr_loop_step_len(const cr_loop_t *lp);   /* the ticks step entry / an empty loop in the editor takes from the
                                                * Length setting: its bars, the whole bars that fit CRL_STEPS steps at
                                                * most (4 of 4/4, 5 of 3/4 and 6/8); 0: Free */
uint32_t cr_loop_pos(const cr_loop_t *lp);     /* the playhead: ticks into the take / the cycle (0 idle) */
void cr_loop_stop(cr_loop_t *lp, cr_t *c);     /* stop playing (loop voices end), capture ends */
void cr_loop_panic(cr_loop_t *lp);             /* after cr_panic: stopped, content kept (a fresh take dropped) */
void cr_loop_set(cr_loop_t *lp, cr_t *c, const crl_data_t *d);    /* load now (stops): d = 0 empties */
void cr_loop_queue(cr_loop_t *lp, cr_t *c, const crl_data_t *d);  /* playing: at the end of the cycle; else now.
                                                * An overdub armed or an undo before then cancels it (the loop
                                                * being edited stays), as a stop or a clear does */
void cr_loop_metro(cr_loop_t *lp, int on);     /* the click on / off (its grid restarts) */
/* the Rate (0..CRL_NRATE-1: x1/16 x1/12 x1/8 x1/6 x1/4 x1/2 x3/4 x1 x1.5 x2 x4; out of range: x1): how fast the
 * loop's own time runs against the tempo. Taken
 * at once when nothing plays and no take runs, else at the loop's next start (the end of the cycle): the position
 * never jumps, a sounding note keeps its length in loop time, and a loop whose cycles ended on a beat starts the new
 * rate on one. The count-in and the click stay the tempo's */
void cr_loop_set_rate(cr_loop_t *lp, int rate);

/* ---- the loop editor (docs/LOOPER.md "The loop editor"). The loop as a pattern of steps of CRL_STEP ticks, whatever
 * made it (step entry, a take played by hand, an older build's step loop): an event belongs to the step it starts in,
 * and keeps its exact time until that step's notes are replaced (nothing is requantised when the editor opens). A
 * step's LENGTH is its notes' length (crl_ev_t.dur), never its place: setting it moves no other event; a long note
 * sounds over the steps after it, which still play (the voices: cr_loop_event's, CR_MAX_LOOPV, the one ending first
 * taken when all are busy). Nothing is stored for the editor: the steps are read from the events each time
 * (cr_loop_ed_map), the record's format is unchanged. The editor works on what sounds (the live events). Its FIRST EDIT
 * makes the loop one layer (crl_ed_flat: the overdubs merged, what a Replace layer had hidden gone for good), so after
 * an edit there is nothing left for the looper's undo to take. Opened and closed without an edit, the loop and its
 * layers are as they were. A loop longer than CRL_STEPS steps (a long take) is shown CRL_STEPS (a page) at a time */
typedef struct {                               /* a page: the CRL_STEPS steps from `first` */
    uint64_t start;                            /* bit s: an event starts in step first + s */
    uint64_t tail;                             /* bit s: none starts there, and a note begun earlier sounds through
                                                * half of it or more */
    uint64_t rat[2];                           /* bits s of rat[0] (low) and rat[1] (high): the step's ratchet, 0 .. 3
                                                * (off, x2 .. x4), where an event starts (the grid's ticks) */
    uint8_t cell[CRL_STEPS];                   /* a step an event starts in (else 0): CRL_EC_VEL(the loudest of its
                                                * events) | its chance << 4 (0 .. 7: CRL_CHANCE_PCT, 0 = 100 %) |
                                                * CRL_EC_COND when it has a condition (not Always): the grid's colour
                                                * and strip (cr_screen.h lm_cell: the same byte). A step past the
                                                * CRL_STEPS the table has (a long take's later pages): the velocity only */
    uint16_t first;
    uint16_t nstep, nall;                      /* steps in the loop; with the steps hidden beyond its end */
} crl_edmap_t;
#define CRL_EC_VEL(v) ((v) >= 123u ? 15u : (v) < 6u ? 1u : ((uint32_t)(v) + 12u) / 9u)   /* a velocity 1 .. 127 as 1 .. 15:
                                                * 12 = the default 100 (96 .. 104), a level every 9 */
#define CRL_EC_COND 0x80u
enum { CRL_ED_OK, CRL_ED_BUSY };
int  cr_loop_ed_open(cr_loop_t *lp);           /* CRL_ED_*: refused while a capture is open. An empty loop: an empty
                                                * pattern of the set Length (cr_loop_step_len; Free: a bar) */
void cr_loop_ed_close(cr_loop_t *lp, cr_t *c); /* the steps hidden beyond the end go; no event left: the loop empty */
void cr_loop_ed_map(const cr_loop_t *lp, uint32_t first, crl_edmap_t *m);
void cr_loop_ed_cursor(cr_loop_t *lp, uint32_t step);   /* the cursor's step (0 ..) */
int  cr_loop_ed_clear(cr_loop_t *lp);          /* the cursor's step: its events go; how many */
uint32_t cr_loop_ed_nlen(const cr_loop_t *lp); /* .. how long its notes sound, in steps (the longest; a part of a step
                                                * counts whole; CRL_STEPS at most); 0: no note */
int  cr_loop_ed_setnlen(cr_loop_t *lp, uint32_t n);   /* .. its notes n steps long (1 .. CRL_STEPS; an off-grid note
                                                * ends on the grid). Nothing else moves. 0: refused (no note, n) */
int  cr_loop_ed_len(cr_loop_t *lp, uint32_t len);     /* the loop's length in ticks (the events beyond it are kept,
                                                * unheard and unsaved, until the editor closes); 0: refused */
int  cr_loop_ed_vel(cr_loop_t *lp, int delta); /* the step's events' velocity; the highest after, 0: no event */
/* the step preview (GitHub issue 8; docs/LOOPER.md "The loop editor", "Step preview"): the cursor's step heard once,
 * as the loop would play it, when the cursor lands on it while the loop is STOPPED: its live events (both sides) each
 * on a loop voice as crl_play_ev strikes them (the velocity by the Level, the step's voicing and ratchet, a bass-side
 * note on the bass part, the notes' own length), its condition and chance not asked (a preview always sounds). A
 * preview sounding is ended first (an empty step: silence). Nothing else: no event, no capture, the position where it
 * was, not counted as played, not the loop's "now playing". While the loop plays, or outside the editor: nothing.
 * Returns the events struck */
int  cr_loop_ed_preview(cr_loop_t *lp, cr_t *c);
int  cr_loop_ed_transpose(cr_loop_t *lp, int semis);   /* the step's notes (chord roots, single notes, bass-side notes)
                                                * semis up or down, chromatically: the events' roots are rewritten, nothing
                                                * else moves and nothing new is stored. 1 done; 0 refused: no event, or
                                                * one of them would leave CRL_ROOT_MIN .. CRL_ROOT_MAX (none moves) */
#define CRL_ROOT_MIN 0
#define CRL_ROOT_MAX 127
uint32_t cr_loop_ed_nfrac(const cr_loop_t *lp);/* the cursor's step: its notes are a part of a step long: 1 .. 3 quarters
                                                * of it (the longest is exactly 6, 12 or 18 ticks); 0: not (cr_loop_ed_nlen) */
int  cr_loop_ed_setnfrac(cr_loop_t *lp, uint32_t f);   /* .. its notes f quarters of a step long (1 .. 3). 0: refused */
/* the cursor step's properties (CRL_SP_*: the ratchet 0 .. 3, the condition CRL_C_*, the voicing 0 Follow, 1 .. 25,
 * the chance 0 .. 7: CRL_CHANCE_PCT).
 * get: -1 = none (no event starts in the step, or a step past the CRL_STEPS the table has: a long take's later pages).
 * set: 1 done, 0 refused (the same; a value out of range; CRL_ED_NOROOM: the loop is so full of events that its
 * record could not hold the step's entry too) */
enum { CRL_SP_RATCH, CRL_SP_COND, CRL_SP_VOIC, CRL_SP_CHANCE };
#define CRL_ED_NOROOM (-2)
int  cr_loop_ed_prop(const cr_loop_t *lp, int which);
int  cr_loop_ed_setprop(cr_loop_t *lp, int which, int v);

/* queries */
uint32_t cr_loop_bar(const cr_loop_t *lp);     /* ticks per bar, per beat (the click) */
uint32_t cr_loop_beat(const cr_loop_t *lp);
uint16_t cr_loop_ring(const cr_loop_t *lp);    /* Q8 position on the ring (0 when idle) */
void cr_loop_where(const cr_loop_t *lp, uint32_t *bar, uint32_t *beat);   /* 1-based; count-in: beats left */
int  cr_loop_layers(const cr_loop_t *lp);
uint32_t cr_loop_rate_q48(const cr_loop_t *lp);/* the Rate in force, 48ths (3 4 6 8 12 24 36 48 72 96 192) */

/* the slot record (docs/LOOPER.md): little-endian bytes, CRL_REC_MAX at most. pack returns its length; unpack
 * returns 1 for a valid record (*d filled), 0 otherwise (*d emptied) */
#define CRL_REC_MAGIC 0x314C5243u              /* "CRL1" */
#define CRL_REC_HDR 16u
#define CRL_RF_PART 1u                         /* byte 7 (flags; 0 until 0.17): a single note's ext nibble is its part
                                                * (CRL_X_BASS). Written only by a loop with a bass-side note */
#define CRL_RF_STEP 2u                         /* byte 7, bit 1 (0.18): the steps' properties follow the events: a count
                                                * u8, then per step {step u8, st[0], st[1]}, ascending, only steps that
                                                * have a property and an event. Written only by a loop that uses one */
#define CRL_REC_MAX (CRL_REC_HDR + 2u * CRL_MAX_LAYERS + 7u * CRL_MAX_EV)   /* (unchanged by CRL_RF_STEP: the entries
                                                * go in the room the events leave, cr_loop_pack) */
uint32_t cr_loop_pack(const crl_data_t *d, uint8_t *buf, uint32_t max);
int  cr_loop_unpack(const uint8_t *buf, uint32_t n, crl_data_t *d);

#endif
