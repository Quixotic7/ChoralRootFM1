/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the engine's three note streams -> Felucca's parts and MIDI (docs/INTEGRATION.md section 2),
 * the engine's clock and its input queue (section 4).
 *
 *   part 0 CHORD  <- stream MAIN  (the chord / performance sound: PRESETS)
 *   part 1 BASS   <- stream BASS  (the bass sound: ALGORITHM, P_VOICE = V_MONO)
 *   RAW           MIDI only (Options can route it to part 0 too)
 *
 * Everything here that touches the engine runs in the audio ISR: cr_audio_block() is called at the top of every
 * CTL-sample block, before fx.c renders it (the mix_block shim in choralroot.c / emu_firmware.h), drains the
 * events the UI posted (cr_post: a lock-free single-producer ring), ticks the engine with the sample clock and
 * emits the MIDI clock. The engine's callbacks below end in voice.c's trk_note_on / trk_note_off and usb.c's
 * midi_out_event, exactly as seq.c's key_on / key_off do. The UI never calls a note path of the engine.
 * Included after voice.c, usb.c (and seq.c on the emulator, which stays inert: song.grid = 2) and cr_engine.c. */

/* --------------------------------------------------------------- routing --- */
#define CR_PART_CHORD 0u
#define CR_PART_BASS 1u
#define CR_NOPART 0xFFu
typedef struct {
    uint8_t part[CR_NSTREAM];        /* the Felucca part a stream plays (CR_NOPART: none) */
    uint8_t midi_en[CR_NSTREAM];     /* the stream goes out on MIDI */
    uint8_t ch[CR_NSTREAM];          /* its MIDI channel 0..15 (Orchid: 1 / 2 / 3) */
    uint8_t clock_out;               /* 24 PPQN MIDI clock out (Options > MIDI Clock: OUT) */
} cr_route_t;
/* written by the UI (Options), read by the ISR: a byte each, a change applies to the next note */
static volatile cr_route_t cr_route = {{CR_PART_CHORD, CR_PART_BASS, CR_NOPART}, {1, 1, 1}, {0, 1, 2}, 0};

static cr_t cr;                                  /* the engine (owned by the audio ISR) */
static cr_loop_t crl;                            /* the looper (cr_loop.c; the audio ISR's too) */
static crl_data_t crl_stage;                     /* a slot loaded by the UI, waiting to be taken (CRE_LOOP) */
static volatile uint8_t crl_stage_busy;          /* 1: posted, 2: queued for the cycle's end; 0: the UI's again */
static volatile uint8_t crl_did;                 /* the last transport result (CRL_DID_*) .. */
static volatile uint32_t crl_did_n;              /* .. and a count of them (the UI's message) */
static uint8_t cr_och[CR_NSTREAM][128];          /* channel + 1 of each sounding MIDI note (its note-off goes there) */
static uint8_t cr_opart[CR_NSTREAM][128];        /* part + 1 of each sounding note (a re-route never strands one) */
static uint32_t cr_now_ms, cr_ms_rem;            /* the engine clock: audio samples -> ms */
static uint32_t cr_clk_acc;                      /* MIDI clock: samples x bpm x 24 since the last pulse */
static volatile uint16_t cr_out_bpm = 120;       /* the tempo the clock pulses at (the UI sets it with the engine's) */
static volatile uint32_t cr_out_notes;           /* note-ons emitted (diagnostics, the emulator's dump) */
static volatile uint8_t cr_ready;                /* the engine is initialised (the ISR may run before the UI's power-on) */

static void cr_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    midi_out_event((status >> 4) | status << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24);
}

static void cr_cb_note_on(void *ud, cr_stream_t s, uint8_t note, uint8_t vel)
{
    uint32_t p = cr_route.part[s];
    (void)ud;
    note &= 0x7Fu;
    if (p < NPART) {
        trk_note_on(&trk[p], note, vel);
        cr_opart[s][note] = (uint8_t)(p + 1u);
    }
    if (cr_route.midi_en[s]) {
        uint32_t ch = cr_route.ch[s] & 15u;
        cr_midi(0x90u | ch, note, vel ? vel : 1u);
        cr_och[s][note] = (uint8_t)(ch + 1u);
    }
    cr_out_notes++;
}

static void cr_cb_note_off(void *ud, cr_stream_t s, uint8_t note)
{
    (void)ud;
    note &= 0x7Fu;
    if (cr_opart[s][note]) {
        trk_note_off(&trk[cr_opart[s][note] - 1u], note);
        cr_opart[s][note] = 0;
    }
    if (cr_och[s][note]) {
        cr_midi(0x80u | (cr_och[s][note] - 1u), note, 0);
        cr_och[s][note] = 0;
    }
}

/* panic (the engine calls it for all three streams): every voice of the part released, CC 123 on the channel */
static void cr_cb_all_off(void *ud, cr_stream_t s)
{
    uint32_t n, p = cr_route.part[s];
    (void)ud;
    for (n = 0; n < 128u; n++) {
        if (cr_opart[s][n] && cr_opart[s][n] - 1u != p)
            trk_all_off(&trk[cr_opart[s][n] - 1u]);
        cr_opart[s][n] = 0;
        if (cr_och[s][n] && cr_och[s][n] - 1u != (cr_route.ch[s] & 15u))
            cr_midi(0xB0u | (cr_och[s][n] - 1u), 123, 0);
        cr_och[s][n] = 0;
    }
    if (p < NPART)
        trk_all_off(&trk[p]);
    cr_midi(0xB0u | (cr_route.ch[s] & 15u), 123, 0);   /* (always: the channel may hold another device's notes) */
}

/* a chord gesture began / ended: the looper's capture */
static void cr_cb_gesture(void *ud, uint32_t gid, int16_t root, uint8_t q, uint8_t ext, uint8_t vel, int on)
{
    (void)ud;
    cr_loop_gesture(&crl, gid, root, q, ext, vel, on);
}

static const cr_out_t CR_OUT = {cr_cb_note_on, cr_cb_note_off, cr_cb_all_off, 0, cr_cb_gesture};

/* ------------------------------------------------------------ the queue --- */
/* UI -> ISR. One producer (the main loop), one consumer (the audio ISR): the index stores are the publication */
enum {
    CRE_KEY,          /* a: note, b: vel, v: down */
    CRE_MOD,          /* a: cr_mod_t, v: down */
    CRE_VOICING,      /* v: +-steps */
    CRE_BASS_VOICING, /* v: +-steps */
    CRE_PANIC,
    CRE_PLAYSTYLE, CRE_EXTADD, CRE_SECRET,
    CRE_KEYMODE,      /* a: on, b: tonic, v: scale */
    CRE_TRANSPOSE,    /* v */
    CRE_BASS, CRE_BASS_MODE,
    CRE_PERFORM, CRE_PERFORM_MODE,
    CRE_PARAM,        /* a: mode, b: param, v: value */
    CRE_STICKY, CRE_TEMPO, CRE_STREAM, /* a: stream, v: on */
    CRE_SINGLE,       /* a: cr_single_t, v: split point 0..11 */
    CRE_LOOP          /* a: LP_*, b / v: its argument */
};
/* CRE_LOOP's operations */
enum { LP_REC, LP_PLAY, LP_UNDO, LP_CLEAR, LP_SET, LP_QUEUE, LP_CONF, LP_METRO };   /* LP_SET / QUEUE: b 1 = empty */
enum { LC_SYNC, LC_QUANT, LC_COUNTIN, LC_LEVEL, LC_SIG, LC_VOL };                  /* LP_CONF: b, v */
typedef struct { uint8_t op, a, b, rsv; int16_t v; } cr_ev_in_t;
#define CR_EVQ 128u                              /* a power of two */
static cr_ev_in_t cr_evq[CR_EVQ];
static volatile uint32_t cr_evq_w, cr_evq_r;
static volatile uint32_t cr_evq_lost;

static int cr_post(uint32_t op, uint32_t a, uint32_t b, int32_t v)
{
    cr_ev_in_t *e;
    if (cr_evq_w - cr_evq_r >= CR_EVQ) {
        cr_evq_lost++;                           /* (a note-off lost here: the engine's panic is the way out) */
        return 0;
    }
    e = &cr_evq[cr_evq_w % CR_EVQ];
    e->op = (uint8_t)op;
    e->a = (uint8_t)a;
    e->b = (uint8_t)b;
    e->v = (int16_t)v;
    RING_PUBLISH();
    cr_evq_w++;
    return 1;
}

static void cr_loop_did(int r)
{
    crl_did = (uint8_t)r;
    crl_did_n++;
}
static void cr_loop_op(const cr_ev_in_t *e)
{
    switch (e->a) {
    case LP_REC: cr_loop_did(cr_loop_rec(&crl, &cr)); break;
    case LP_PLAY: cr_loop_did(cr_loop_play(&crl, &cr)); break;
    case LP_UNDO: cr_loop_did(cr_loop_undo(&crl, &cr)); break;
    case LP_CLEAR: cr_loop_did(cr_loop_clear(&crl, &cr)); break;
    case LP_SET:
        cr_loop_set(&crl, &cr, e->b ? 0 : &crl_stage);
        crl_stage_busy = 0;
        break;
    case LP_QUEUE:
        cr_loop_queue(&crl, &cr, e->b ? 0 : &crl_stage);
        crl_stage_busy = crl.next_on ? 2u : 0u;    /* (the block frees it once the switch is taken) */
        break;
    case LP_METRO: cr_loop_metro(&crl, e->v); break;
    case LP_CONF:
        switch (e->b) {
        case LC_SYNC: crl.sync = (uint8_t)(e->v < (int)CRL_NSYNC ? e->v : 0); break;
        case LC_QUANT: crl.quant = (uint8_t)(e->v < (int)CRL_NQUANT ? e->v : 0); break;
        case LC_COUNTIN: crl.count_in = (uint8_t)(e->v != 0); break;
        case LC_LEVEL: crl.level = (uint8_t)(e->v > 100 ? 100 : e->v); break;
        case LC_SIG: if (crl.cap == CRL_CAP_NONE) crl.sig = (uint8_t)(e->v < CRL_NSIG ? e->v : 0); break;
        case LC_VOL: crl.metro_vol = (uint8_t)(e->v > 100 ? 100 : e->v); break;
        default: break;
        }
        break;
    default: break;
    }
}

static void cr_apply(const cr_ev_in_t *e)
{
    switch (e->op) {
    case CRE_KEY: cr_key(&cr, e->a, e->b, e->v); break;
    case CRE_MOD: cr_mod(&cr, (cr_mod_t)e->a, e->v); break;
    case CRE_VOICING: cr_voicing_step(&cr, e->v); break;
    case CRE_BASS_VOICING: cr_bass_voicing_step(&cr, e->v); break;
    case CRE_PANIC: cr_panic(&cr); cr_loop_panic(&crl); break;
    case CRE_PLAYSTYLE: cr_set_playstyle(&cr, (cr_playstyle_t)e->v); break;
    case CRE_EXTADD: cr_set_ext_addition(&cr, (cr_extadd_t)e->v); break;
    case CRE_SECRET: cr_set_secret(&cr, (cr_secret_t)e->v); break;
    case CRE_KEYMODE: cr_set_key(&cr, e->a, e->b, (cr_scale_t)e->v); break;
    case CRE_TRANSPOSE: cr_set_transpose(&cr, e->v); break;
    case CRE_BASS: cr_set_bass(&cr, e->v); break;
    case CRE_BASS_MODE: cr_set_bass_mode(&cr, (cr_bassmode_t)e->v); break;
    case CRE_PERFORM: cr_set_perform(&cr, e->v); break;
    case CRE_PERFORM_MODE: cr_set_perform_mode(&cr, (cr_pmode_t)e->v); break;
    case CRE_PARAM: cr_set_param(&cr, (cr_pmode_t)e->a, (cr_eparam_t)e->b, e->v); break;
    case CRE_STICKY: cr_set_sticky(&cr, e->v); break;
    case CRE_TEMPO: cr_set_tempo(&cr, e->v); cr_out_bpm = (uint16_t)e->v; break;
    case CRE_STREAM: cr_set_stream(&cr, (cr_stream_t)e->a, e->v); break;
    case CRE_SINGLE: cr_set_single_notes(&cr, (cr_single_t)e->a, e->v); break;
    case CRE_LOOP: cr_loop_op(e); break;
    default: break;
    }
}

/* ------------------------------------------------------- the audio side --- */
static void cr_out_init(void)                    /* power-on (the audio ISR may already run: IRQ off) */
{
    fm1_irq_off();
    cr_ready = 0;
    cr_init(&cr, &CR_OUT);
    cr_seed(&cr, 0x43524631u);
    cr_loop_init(&crl);
    crl_stage_busy = 0;
    cr_now_ms = cr_ms_rem = cr_clk_acc = 0;
    cr_evq_r = cr_evq_w = 0;
    cr_ready = 1;
    fm1_irq_on();
}

/* the audio ISR, once per CTL-sample block, before the block renders: input, the scheduler, the clock */
static void cr_audio_block(uint32_t n)
{
    uint32_t w = cr_evq_w;
    if (!cr_ready)
        return;
    while (cr_evq_r != w) {
        cr_apply(&cr_evq[cr_evq_r % CR_EVQ]);
        cr_evq_r++;
    }
    cr_ms_rem += n * 1000u;                      /* samples -> ms, the remainder kept (no drift) */
    while (cr_ms_rem >= FS) {
        cr_ms_rem -= FS;
        cr_now_ms++;
    }
    cr_tick(&cr, cr_now_ms);
    cr_loop_tick(&crl, &cr, cr_now_ms);          /* the loop's playback and capture, the click */
    if (crl_stage_busy == 2u && !crl.next_on)
        crl_stage_busy = 0;
    if (cr_route.clock_out) {                    /* 24 PPQN: a pulse every FS * 60 / (bpm * 24) samples */
        cr_clk_acc += n * (uint32_t)cr_out_bpm * 24u;
        while (cr_clk_acc >= FS * 60u) {
            cr_clk_acc -= FS * 60u;
            midi_out_event(0x0Fu | 0xF8u << 8);
        }
    } else {
        cr_clk_acc = 0;
    }
}

/* the metronome's click (crl.click, set by the loop's clock in this block): a decaying sine, 1.5 kHz on a beat and
 * 2.5 kHz on the bar's first, from a two-pole resonator (Q14), mixed after fx.c's master (the mix_block shim) */
static int32_t cr_ck_y1, cr_ck_y2, cr_ck_a1;
static uint32_t cr_ck_n;
#define CR_CK_A2 16335                           /* r^2, r = 0.9985 per sample: ~25 ms */
static void cr_click_mix(int32_t *out, uint32_t n)
{
    uint32_t i;
    if (crl.click) {
        int32_t amp = 10000 * (int32_t)crl.metro_vol / 100;
        if (crl.click == 2u) { cr_ck_a1 = 30665; cr_ck_y1 = amp * 3487 / 10000; }   /* 2 r cos w, amp sin w */
        else { cr_ck_a1 = 32011; cr_ck_y1 = amp * 2121 / 10000; }
        cr_ck_y2 = 0;
        cr_ck_n = amp ? 2400u : 0u;
        crl.click = 0;
    }
    for (i = 0; i < n && cr_ck_n; i++, cr_ck_n--) {
        int32_t y = ((cr_ck_a1 * cr_ck_y1) >> 14) - ((CR_CK_A2 * cr_ck_y2) >> 14);
        cr_ck_y2 = cr_ck_y1;
        cr_ck_y1 = y;
        out[2u * i] += y;
        out[2u * i + 1u] += y;
    }
}

/* the UI's copy of what the screen and the LEDs show, taken with the audio IRQ off (cr_engine is not reentrant) */
typedef struct {
    cr_chord_info_t ci;
    uint8_t mods_held, mods_active, latching, perform_on, perform_mode, bass_on, playstyle;
    int8_t voicing, bass_voicing;
    int perf_pos;
    uint8_t perf_note;
    uint16_t scale_mask;
    int voices, pending;
    uint8_t sound[128];              /* bit s: stream s sounds note n (registry) */
    /* the looper */
    uint8_t lstate, lcap, lnlayers, lfull, lnext, ldirty, lsig;
    uint16_t lnev, lring;
    uint32_t llen, lbar, lbeat, lplayed;
} cr_snap_t;
static cr_snap_t cr_snap;

static void cr_snapshot(void)
{
    uint32_t n;
    fm1_irq_off();
    cr_chord_info(&cr, &cr_snap.ci);
    cr_snap.mods_held = cr_mods_held(&cr);
    cr_snap.mods_active = cr_mods_active(&cr);
    cr_snap.latching = (uint8_t)cr_latching(&cr);
    cr_snap.perform_on = cr.perform_on;
    cr_snap.perform_mode = cr.perform_mode;
    cr_snap.bass_on = cr.bass_on;
    cr_snap.playstyle = cr.playstyle;
    cr_snap.voicing = cr.voicing;
    cr_snap.bass_voicing = cr.bass_voicing;
    cr_snap.perf_pos = cr_perform_pos(&cr, &cr_snap.perf_note);
    cr_snap.scale_mask = cr_scale_mask(&cr);
    cr_snap.voices = cr_voices(&cr);
    cr_snap.pending = cr_pending(&cr);
    for (n = 0; n < 128u; n++)
        cr_snap.sound[n] = (uint8_t)((cr.reg[0][n] ? 1u : 0u) | (cr.reg[1][n] ? 2u : 0u) | (cr.reg[2][n] ? 4u : 0u));
    cr_snap.lstate = crl.state;
    cr_snap.lcap = crl.cap;
    cr_snap.lnlayers = crl.d.nlayers;
    cr_snap.lfull = crl.full;
    cr_snap.lnext = crl.next_on;
    cr_snap.ldirty = crl.dirty;
    cr_snap.lsig = crl.sig;
    cr_snap.lnev = crl.d.nev;
    cr_snap.llen = crl.d.len;
    cr_snap.lring = cr_loop_ring(&crl);
    cr_loop_where(&crl, &cr_snap.lbar, &cr_snap.lbeat);
    cr_snap.lplayed = crl.played;
    fm1_irq_on();
}

/* parts 0 / 1: voices still sounding (gate or release): the "no stuck notes" check */
static uint32_t cr_parts_busy(void)
{
    uint32_t p, i, n = 0;
    for (p = 0; p < 2u; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].active;
    return n;
}
