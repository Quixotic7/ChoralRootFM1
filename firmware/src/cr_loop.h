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

enum { CRL_EMPTY, CRL_STOPPED, CRL_PLAYING };                  /* the loop */
enum { CRL_CAP_NONE,                                            /* capture */
       CRL_CAP_ARMED,       /* Free, a new loop: the first chord starts it (REC lit) */
       CRL_CAP_COUNTIN,     /* synced: the one-bar count-in (REC blinks, the click counts) */
       CRL_CAP_REC,         /* recording the first layer (REC blinks) */
       CRL_CAP_OD_ARMED,    /* playing, overdub armed: the next chord opens a layer (REC lit) */
       CRL_CAP_OD };        /* overdubbing a layer (REC blinks) */
enum { CRL_SIG_44, CRL_SIG_34, CRL_SIG_68, CRL_NSIG };

typedef struct { uint16_t t, dur; uint8_t root, vel, qx, layer; } crl_ev_t;   /* qx = quality << 4 | ext */

typedef struct {                 /* a loop's content: what a slot stores */
    uint32_t len;                /* ticks, 0 = empty */
    uint16_t nev;
    uint8_t nlayers, sig;        /* sig: the time signature it was made in (CRL_SIG_*) */
    crl_ev_t ev[CRL_MAX_EV];     /* in layer order (a layer's events follow the previous layer's) */
} crl_data_t;

typedef struct {
    /* settings (the UI's, posted) */
    uint8_t sync, quant, count_in, level;      /* 0..5, 0..6, 0/1, 0..100 (loop velocity %) */
    uint8_t sig, metro, metro_vol;             /* CRL_SIG_*, click on, click level 0..100 */
    /* the loop */
    crl_data_t d;
    uint8_t state, cap, full;
    uint16_t layer_ev0;                        /* the capture layer's first event */
    /* the clock: ticks at the engine's BPM, from the ms clock */
    uint32_t t, acc, last_ms;
    uint8_t clk_on;
    uint32_t cyc0, scan;                       /* the current cycle's start (abs ticks); next position to play */
    uint32_t rec0, rec_len, cin0, od0, met0;
    struct { uint32_t gid, t0; int16_t root; uint8_t used, q, ext, vel; } open[CRL_OPEN];   /* held gestures */
    struct { uint32_t off; uint8_t used; } pv[CR_MAX_LOOPV];
    const crl_data_t *next;                    /* a slot switch waiting for the end of the cycle (0: empty) */
    uint8_t next_on, dirty;                    /* a switch is queued; the content changed since loaded / saved */
    /* out: the click (the audio side takes it), diagnostics */
    uint8_t click;                             /* 0 none, 1 beat, 2 the bar's first beat */
    uint32_t played;                           /* events played (diagnostics) */
} cr_loop_t;

void cr_loop_init(cr_loop_t *lp);
void cr_loop_tick(cr_loop_t *lp, cr_t *c, uint32_t now_ms);
/* cr_out_t.gesture's body */
void cr_loop_gesture(cr_loop_t *lp, uint32_t gid, int16_t root, uint8_t q, uint8_t ext, uint8_t vel, int on);

/* transport (PLAN.md section 3). Each returns what it did (CRL_DID_*) for the UI's message */
enum { CRL_DID_NOTHING, CRL_DID_ARM, CRL_DID_COUNTIN, CRL_DID_REC, CRL_DID_CANCEL, CRL_DID_COMMIT, CRL_DID_OD_ARM,
       CRL_DID_OD_END, CRL_DID_PLAY, CRL_DID_STOP, CRL_DID_UNDO, CRL_DID_CLEAR, CRL_DID_EMPTY_TAKE };
int  cr_loop_rec(cr_loop_t *lp, cr_t *c);      /* REC tap */
int  cr_loop_play(cr_loop_t *lp, cr_t *c);     /* LOOP tap */
int  cr_loop_undo(cr_loop_t *lp, cr_t *c);     /* REC hold, F#4: the last layer (an overdub in progress first) */
int  cr_loop_clear(cr_loop_t *lp, cr_t *c);    /* D#4 held 1 s */
void cr_loop_stop(cr_loop_t *lp, cr_t *c);     /* stop playing (loop voices end), capture ends */
void cr_loop_panic(cr_loop_t *lp);             /* after cr_panic: stopped, content kept (a fresh take dropped) */
void cr_loop_set(cr_loop_t *lp, cr_t *c, const crl_data_t *d);    /* load now (stops): d = 0 empties */
void cr_loop_queue(cr_loop_t *lp, cr_t *c, const crl_data_t *d);  /* playing: at the end of the cycle; else now */
void cr_loop_metro(cr_loop_t *lp, int on);     /* the click on / off (its grid restarts) */

/* queries */
uint32_t cr_loop_bar(const cr_loop_t *lp);     /* ticks per bar, per beat (the click) */
uint32_t cr_loop_beat(const cr_loop_t *lp);
uint16_t cr_loop_ring(const cr_loop_t *lp);    /* Q8 position on the ring (0 when idle) */
void cr_loop_where(const cr_loop_t *lp, uint32_t *bar, uint32_t *beat);   /* 1-based; count-in: beats left */
int  cr_loop_layers(const cr_loop_t *lp);

/* the slot record (docs/LOOPER.md): little-endian bytes, CRL_REC_MAX at most. pack returns its length; unpack
 * returns 1 for a valid record (*d filled), 0 otherwise (*d emptied) */
#define CRL_REC_MAGIC 0x314C5243u              /* "CRL1" */
#define CRL_REC_HDR 16u
#define CRL_REC_MAX (CRL_REC_HDR + 2u * CRL_MAX_LAYERS + 7u * CRL_MAX_EV)
uint32_t cr_loop_pack(const crl_data_t *d, uint8_t *buf, uint32_t max);
int  cr_loop_unpack(const uint8_t *buf, uint32_t n, crl_data_t *d);

#endif
