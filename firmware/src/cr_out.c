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
 * midi_out_event, exactly as Felucca's seq.c key_on / key_off did. The UI never calls a note path of the engine.
 * Included after voice.c, fx.c, usb.c and cr_engine.c.
 *
 * FELUCCA_SEQ 0 (choralroot.c, the emulator, tests/cr_trans_test.c): Felucca's sequencer (seq.c with song_chain.c,
 * chord.c, motion.c, midi_control.c, midi_clock.c) is not in the unit. What the kept files called of it is here, under
 * the same names (docs/INTEGRATION.md section 1): events_block (fx.c, every block: the engine switches, the releases
 * a sound load asks for with panic_req), midi_event (the parts' MIDI in, below), and the few names upreset.c, main.c
 * and audio.c still use (transport_req, trk_index, drum_track, motion_guard / motion_base_value, chain_defaults). */

#if !FELUCCA_SEQ
/* ---------------------------------------------- what seq.c provided --- */
static volatile uint8_t panic_req;               /* bit per part: its notes released (a sound load, M-UPGRADE) */
static const uint8_t transport_req = 0;          /* (upreset.c, cr_bank.c: Felucca's transport never runs) */
#define chain_defaults(c) ((void)0)              /* (main.c felucca_init: no song chain) */
#ifdef FM1_INPUT_LAT
static uint32_t kb_out_tick;                     /* (audio.c: seq.c's key latency stamp; nothing reads it here) */
#endif
static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }
static int drum_track(const track_t *t) { return ENGINES[eng_idx(t->eng_req)] == &ENG_DRUM; }
/* motion.c's: no motion lanes, so a part's sounding value is its saved one; the guard keeps the audio IRQ out while
 * a part's parameters are copied (cr_bank.c fm4_apply), as motion.c's did */
static int16_t motion_base_value(const track_t *t, uint32_t id) { return t->p[id]; }
static uint32_t motion_guard(void)
{
#if defined(FM1_IRQ_TARGET)
    uint32_t f = fm1_icfg();
    fm1_irq_off();
    return f;
#else
    return 0;
#endif
}
static void motion_unguard(uint32_t f)
{
#if defined(FM1_IRQ_TARGET)
    fm1_icfg_set(f);
#else
    (void)f;
#endif
}

/* ------------------------------------------------- the parts' MIDI in --- */
/* cr_midi_in (below) forwards a CHORD / BASS channel's notes, pedal, bend, pressure and the other CCs here as
 * midi_event(status, part, d1, d2), as it handed them to midi_control.c before; what of that ChoralRoot uses:
 *   note on / off       trk_note_on / trk_note_off on the part (a repeated note-on restarts it; velocity 0: off)
 *   CC 64 sustain       a released note is held while the pedal is down, released with it
 *   pitch bend          voice.c's midi_bend_target, +-2 semitones (RPN 0 sets 0..24 semitones, 0..99 cents); FM6 bends by its
 *                       own BEND range (track_t.bend_raw, docs/FM6.md)
 *   CC 1 / 11, pressure mod.c's MODW / EXPR / AT sources (mod_midi)
 *   CC 120              all sound off: the part's voices cut (a short fade), the pedal ignored
 *   CC 121              reset controllers: bend, wheel, pressure, expression, the pedal up (the bend range kept)
 *   CC 123              all notes off: the part's MIDI notes released (the pedal honoured), the part's voices too
 *                       when no MIDI note is held then
 * Felucca's chords (CHRD), its arpeggiator and live recording were the sequencer's: gone. A part's MIDI notes and the
 * engine's stream share its voices exactly as before (a MIDI note-off ends that note, whoever started it). */
#define CR_MPED 0x80u                            /* cr_mnote: the key is up, the pedal holds the note */
typedef struct {
    int16_t bend;                                /* signed 14-bit, 0 = centre */
    uint8_t pedal, semis, cents, rpn_msb, rpn_lsb, ready;
} cr_mch_t;
static cr_mch_t cr_mch[2];                       /* parts 0 / 1 (CHORD / BASS) */
static uint8_t cr_mnote[2][128];                 /* 1: a MIDI note-on holds it; | CR_MPED: the pedal does */
static uint8_t cr_mheld[2];                      /* notes cr_mnote holds per part (CC 123's check), at most 128 */

static cr_mch_t *cr_mchan(uint32_t p)
{
    cr_mch_t *c = &cr_mch[p];
    if (!c->ready) {
        c->semis = 2;
        c->rpn_msb = c->rpn_lsb = 127;
        c->ready = 1;
    }
    return c;
}
static void cr_mbend(uint32_t p)                 /* the channel's bend -> the part's (voice.c glides to it) */
{
    const cr_mch_t *c = cr_mchan(p);
    int32_t range = ((int32_t)c->semis * 100 + c->cents) * 256 / 100;
    midi_bend_target[p] = (int32_t)c->bend * range / (c->bend < 0 ? 8192 : 8191);
    trk[p].bend_raw = c->bend;                   /* (FM6 bends by its own BEND range: its patch's, docs/FM6.md) */
}
static void cr_mrelease(uint32_t p, uint32_t note)
{
    if (cr_mnote[p][note]) {
        cr_mnote[p][note] = 0;
        if (cr_mheld[p])
            cr_mheld[p]--;
        trk_note_off(&trk[p], note);
    }
}
static void cr_mpedal_up(uint32_t p)
{
    uint32_t n;
    cr_mchan(p)->pedal = 0;
    for (n = 0; n < 128u; n++)
        if (cr_mnote[p][n] & CR_MPED)
            cr_mrelease(p, n);
}
static void cr_mforget(uint32_t p)               /* a panic: the part's MIDI notes forgotten (their offs end nothing) */
{
    if (p < 2u) {
        memset(cr_mnote[p], 0, sizeof cr_mnote[p]);
        cr_mheld[p] = 0;
    }
    midi_bend_q8[p] = midi_bend_target[p] = 0;
    trk[p].bend_raw = 0;
}
static void cr_mcontrol(uint32_t p, uint32_t cc, uint32_t v)
{
    cr_mch_t *c = cr_mchan(p);
    track_t *t = &trk[p];
    uint32_t n;
    switch (cc) {
    case 64:
        if (v >= 64u)
            c->pedal = 1;
        else
            cr_mpedal_up(p);
        break;
    case 120:                                    /* all sound off: ignores the pedal */
        trk_all_off(t);
        for (n = 0; n < NVOICE; n++)
            if (t->v[n].active)
                voice_kill(&t->v[n]);
        cr_mforget(p);
        break;
    case 121:                                    /* reset all controllers (mod_midi: wheel, pressure, expression) */
        c->bend = 0;
        c->rpn_msb = c->rpn_lsb = 127;
        cr_mbend(p);
        cr_mpedal_up(p);
        break;
    case 123:                                    /* all notes off: normal releases, the pedal honoured */
        for (n = 0; n < 128u; n++)
            if (cr_mnote[p][n] && !(cr_mnote[p][n] & CR_MPED)) {
                if (c->pedal)
                    cr_mnote[p][n] |= CR_MPED;
                else
                    cr_mrelease(p, n);
            }
        if (!cr_mheld[p])
            trk_all_off(t);
        break;
    case 101: c->rpn_msb = (uint8_t)v; break;
    case 100: c->rpn_lsb = (uint8_t)v; break;
    case 99: case 98: c->rpn_msb = c->rpn_lsb = 127; break;   /* NRPN selected: no RPN data entry */
    case 6: case 38:
        if (!c->rpn_msb && !c->rpn_lsb) {        /* RPN 0, pitch bend sensitivity */
            if (cc == 6u)
                c->semis = (uint8_t)(v > 24u ? 24u : v);
            else
                c->cents = (uint8_t)(v > 99u ? 99u : v);
            cr_mbend(p);
        }
        break;
    default: break;
    }
}
/* st: the status' high nibble (0x80..0xE0); p: the part 0 / 1 (cr_midi.c crm_map) */
static void __attribute__((noinline)) midi_event(uint32_t st, uint32_t p, uint32_t d1, uint32_t d2)
{
    if (p >= 2u)
        return;
    if (st == 0x90u && d2) {
        if (cr_mnote[p][d1])                     /* a repeated note replaces the previous press (a pedal-held one too) */
            cr_mrelease(p, d1);
        cr_mbend(p);
        trk_note_on(&trk[p], d1, d2);
        cr_mnote[p][d1] = 1;
        if (cr_mheld[p] < 128u)
            cr_mheld[p]++;
    } else if (st == 0x80u || st == 0x90u) {
        if (cr_mnote[p][d1] && cr_mchan(p)->pedal)
            cr_mnote[p][d1] |= CR_MPED;
        else
            cr_mrelease(p, d1);
    } else if (st == 0xE0u) {
        cr_mchan(p)->bend = (int16_t)((int32_t)(d1 | d2 << 7) - 8192);
        cr_mbend(p);
    } else if (st == 0xB0u) {
        mod_midi(&trk[p], st, d1, d2);
        cr_mcontrol(p, d1, d2);
    } else if (st == 0xD0u) {
        mod_midi(&trk[p], st, d1, d2);
    }
}

/* fx.c's, between two rendered blocks (after cr_audio_block): the releases asked for, the engine switches. A MIDI in
 * queue that overflowed (a lost note-off) is dropped and every part released: no stuck note */
static void events_block(uint32_t n)
{
    uint32_t i, pr = panic_req;
    (void)n;
    panic_req = 0;
    if (midi_in_overflow) {
        mi_r = mi_w;
        pr |= (1u << NTRK) - 1u;
        RING_PUBLISH();
        midi_in_overflow = 0;
    }
    for (i = 0; i < NTRK; i++) {
        if ((pr >> i) & 1u) {
            if (i < 2u)
                cr_mchan(i)->pedal = 0;
            cr_mforget(i);
            trk_all_off(&trk[i]);
        }
        engine_block(&trk[i]);                   /* engine switch: fade, then switch (voice.c) */
    }
}
#endif /* !FELUCCA_SEQ */

/* --------------------------------------------------------------- routing --- */
#define CR_PART_CHORD 0u
#define CR_PART_BASS 1u
#define CR_NOPART 0xFFu
typedef struct {
    uint8_t part[CR_NSTREAM];        /* the Felucca part a stream plays (CR_NOPART: none) */
    uint8_t midi_en[CR_NSTREAM];     /* the stream goes out on MIDI */
    uint8_t ch[CR_NSTREAM];          /* its MIDI channel 0..15 (Orchid: 1 / 2 / 3) */
    uint8_t clock_out;               /* 24 PPQN MIDI clock out (Options > MIDI Clock: OUT) */
    uint8_t clock_in;                /* follow 0xF8 / FA / FB / FC in (Options > MIDI Clock: IN; nothing is sent) */
} cr_route_t;
/* written by the UI (Options), read by the ISR: a byte each, a change applies to the next note */
static volatile cr_route_t cr_route = {{CR_PART_CHORD, CR_PART_BASS, CR_NOPART}, {1, 1, 1}, {0, 1, 2}, 0, 0};

static cr_t cr;                                  /* the engine (owned by the audio ISR) */
static cr_loop_t crl;                            /* the looper (cr_loop.c; the audio ISR's too) */
static crl_data_t crl_stage;                     /* a slot loaded by the UI, waiting to be taken (CRE_LOOP) */
static volatile uint8_t crl_stage_busy;          /* 1: posted, 2: queued for the cycle's end; 0: the UI's again */
static volatile uint8_t crl_did;                 /* the last transport result (CRL_DID_*) .. */
static volatile uint32_t crl_did_n;              /* .. and a count of them (the UI's message) */
/* each sounding note of a stream: its MIDI channel + 1 (bits 0-4: its note-off goes there) and its part + 1 (bits 5-7:
 * a re-route never strands one); one byte for both (RAM) */
static uint8_t cr_ono[CR_NSTREAM][128];
#define CR_OCH(s, n) (cr_ono[s][n] & 31u)
#define CR_OPART(s, n) ((uint32_t)cr_ono[s][n] >> 5)
#define CR_OCH_SET(s, n, v) (cr_ono[s][n] = (uint8_t)((cr_ono[s][n] & 0xE0u) | (v)))
#define CR_OPART_SET(s, n, v) (cr_ono[s][n] = (uint8_t)((cr_ono[s][n] & 31u) | (v) << 5))
static uint32_t cr_now_ms, cr_ms_rem;            /* the engine clock: audio samples -> ms */
static uint32_t cr_clk_acc;                      /* MIDI clock: samples x bpm x 24 since the last pulse */
static volatile uint16_t cr_out_bpm = 120;       /* the tempo the clock pulses at (the UI sets it with the engine's) */
static volatile uint32_t cr_out_notes;           /* note-ons emitted (diagnostics, the emulator's dump) */
static volatile uint8_t cr_ready;                /* the engine is initialised (the ISR may run before the UI's power-on) */

static void cr_midi(uint32_t status, uint32_t d1, uint32_t d2)
{
    midi_out_event((status >> 4) | status << 8 | (d1 & 0x7Fu) << 16 | (d2 & 0x7Fu) << 24);
}

/* TRANSPOSE (the part's P_TRANS, MIX page): the part's notes play P_TRANS semitones away (clamped to 0..127). The
 * MIDI out of the stream is not transposed: it carries the harmony as played. A part's transposition is taken when
 * none of its notes from here sound (cr_tr_n: started and not ended) and kept while any does, so every note-off
 * ends the note its note-on started; a change applies from the next chord after a release */
static int8_t cr_tr[NPART];                      /* the transposition the part's sounding notes started with */
static uint8_t cr_tr_n[NPART];                   /* .. how many of them sound */
static uint32_t cr_tr_note(uint32_t p, uint32_t note)
{
    int32_t x = (int32_t)note + cr_tr[p];
    return (uint32_t)(x < 0 ? 0 : x > 127 ? 127 : x);
}
static void cr_tr_end(uint32_t p)                /* a note of part p ended */
{
    if (p < NPART && cr_tr_n[p])
        cr_tr_n[p]--;
}

static void cr_cb_note_on(void *ud, cr_stream_t s, uint8_t note, uint8_t vel)
{
    uint32_t p = cr_route.part[s], old;
    (void)ud;
    note &= 0x7Fu;
    if (p < NPART) {
        old = CR_OPART(s, note);
        if (old != p + 1u) {                     /* (a note already on for this part: counted once) */
            if (old)
                cr_tr_end(old - 1u);
            if (!cr_tr_n[p]) {
                int32_t tp = clamp(trk[p].p[P_TRANS], -24, 24);
#if defined(CR_TRACE) && CR_TRACE
                if (tp != cr_tr[p])
                    printf("transpose: part %u %+d\n", (unsigned)p, (int)tp);
#endif
                cr_tr[p] = (int8_t)tp;
            }
            if (cr_tr_n[p] < 255u)
                cr_tr_n[p]++;
        }
        trk_note_on(&trk[p], cr_tr_note(p, note), vel);
        CR_OPART_SET(s, note, p + 1u);
    }
    if (cr_route.midi_en[s]) {
        uint32_t ch = cr_route.ch[s] & 15u;
        cr_midi(0x90u | ch, note, vel ? vel : 1u);
        CR_OCH_SET(s, note, ch + 1u);
    }
    cr_out_notes++;
}

static void cr_cb_note_off(void *ud, cr_stream_t s, uint8_t note)
{
    (void)ud;
    note &= 0x7Fu;
    if (CR_OPART(s, note)) {
        uint32_t p = CR_OPART(s, note) - 1u;
        trk_note_off(&trk[p], cr_tr_note(p, note));
        cr_tr_end(p);
        CR_OPART_SET(s, note, 0u);
    }
    if (CR_OCH(s, note)) {
        cr_midi(0x80u | (CR_OCH(s, note) - 1u), note, 0);
        CR_OCH_SET(s, note, 0u);
    }
}

/* panic (the engine calls it for all three streams): every voice of the part released, CC 123 on the channel */
static void cr_cb_all_off(void *ud, cr_stream_t s)
{
    uint32_t n, p = cr_route.part[s];
    (void)ud;
    for (n = 0; n < 128u; n++) {
        if (CR_OPART(s, n))
            cr_tr_end(CR_OPART(s, n) - 1u);
        if (CR_OPART(s, n) && CR_OPART(s, n) - 1u != p)
            trk_all_off(&trk[CR_OPART(s, n) - 1u]);
        if (CR_OCH(s, n) && CR_OCH(s, n) - 1u != (cr_route.ch[s] & 15u))
            cr_midi(0xB0u | (CR_OCH(s, n) - 1u), 123, 0);
        cr_ono[s][n] = 0;
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
enum { LP_REC, LP_PLAY, LP_UNDO, LP_CLEAR, LP_SET, LP_QUEUE, LP_CONF, LP_METRO,
       LP_STEP };                                                /* LP_SET / QUEUE: b 1 = empty; LP_STEP: v CRL_STEP_* */
enum { LC_SYNC, LC_QUANT, LC_COUNTIN, LC_LEVEL, LC_SIG, LC_VOL, LC_MODE };         /* LP_CONF: b, v */
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
    case LP_STEP: cr_loop_step(&crl, e->v); break;
    case LP_CONF:
        switch (e->b) {
        case LC_SYNC: crl.sync = (uint8_t)(e->v < (int)CRL_NSYNC ? e->v : 0); break;
        case LC_QUANT: crl.quant = (uint8_t)(e->v < (int)CRL_NQUANT ? e->v : 0); break;
        case LC_COUNTIN: crl.count_in = (uint8_t)(e->v != 0); break;
        case LC_LEVEL: crl.level = (uint8_t)(e->v > 100 ? 100 : e->v); break;
        case LC_SIG: if (crl.cap == CRL_CAP_NONE) crl.sig = (uint8_t)(e->v < CRL_NSIG ? e->v : 0); break;
        case LC_VOL: crl.metro_vol = (uint8_t)(e->v > 100 ? 100 : e->v); break;
        case LC_MODE: crl.mode = (uint8_t)(e->v < (int)CRL_NMODE ? e->v : 0); break;
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

/* ------------------------------------------------------------- MIDI in --- */
/* usb.c's midi_in_q (USB and TRS) is drained here, in the audio ISR, before fx.c's events_block runs in the same block
 * (its overflow recovery stays there: on an overflow the queue is left to it).
 *   0xF8 / FA / FB / FC   Options > MIDI Clock = In: the tempo (cr_midi.c's follower -> cr_set_tempo) and the loop's
 *                         transport (the LP_PLAY path); otherwise ignored
 *   the CHORD / BASS channels (Options' channels; Off: ignored): CC 7 / 91 / 93 / 94 and program changes go to the UI
 *                         (cr_min_q: it applies them as a knob would, with the meter); notes, pedal, bend, pressure and
 *                         the other CCs (123: all notes off) go to midi_event (above) for part 0 or 1
 *   other channels        ignored */
#include "cr_midi.c"
static crm_clock_t cr_cin;
static uint8_t cr_cin_mode;                      /* clock_in as last seen (a change resets the follower) */
static volatile uint16_t cr_in_bpm;              /* the tempo the clock set (the UI mirrors it into cs.bpm) .. */
static volatile uint32_t cr_in_bpm_n;            /* .. and a count of its changes */
static volatile uint32_t cr_in_rt_n;             /* start / stop received (In) */
static volatile uint8_t cr_in_rt_last;
#define CR_MINQ 32u                              /* ISR -> UI: CC / program change (a power of two) */
static crm_act_t cr_minq[CR_MINQ];
static volatile uint32_t cr_minq_w, cr_minq_r;

static void cr_min_post(const crm_act_t *a)
{
    if (cr_minq_w - cr_minq_r >= CR_MINQ)
        return;                                  /* (a burst of controllers: the latest values lost, nothing stuck) */
    cr_minq[cr_minq_w % CR_MINQ] = *a;
    RING_PUBLISH();
    cr_minq_w++;
}
static int cr_min_take(crm_act_t *a)             /* the UI */
{
    if (cr_minq_r == cr_minq_w)
        return 0;
    *a = cr_minq[cr_minq_r % CR_MINQ];
    RING_PUBLISH();
    cr_minq_r++;
    return 1;
}

static void cr_loop_op(const cr_ev_in_t *e);
static void cr_in_transport(uint32_t st)         /* FA start / FB continue: the loop plays (FA: from its start); FC */
{
    cr_ev_in_t e = {CRE_LOOP, LP_PLAY, 0, 0, 0};
    int n;
    cr_in_rt_last = (uint8_t)st;
    cr_in_rt_n++;
    n = crm_transport(st, crl.state == CRL_PLAYING, crl.state == CRL_STOPPED, crl.cap != CRL_CAP_NONE);
    while (n-- > 0)
        cr_loop_op(&e);
}

static void cr_midi_in(void)
{
    uint32_t w = mi_w;
    uint8_t en[2], ch[2];
    if (cr_cin_mode != cr_route.clock_in) {
        cr_cin_mode = cr_route.clock_in;
        crm_clock_reset(&cr_cin);
    }
    if (midi_in_overflow)                        /* events_block's recovery: the queue dropped, every part released */
        return;
    en[0] = cr_route.midi_en[CR_STREAM_MAIN];
    en[1] = cr_route.midi_en[CR_STREAM_BASS];
    ch[0] = cr_route.ch[CR_STREAM_MAIN];
    ch[1] = cr_route.ch[CR_STREAM_BASS];
    while (mi_r != w) {
        uint32_t at = mi_r % MQ, pkt = midi_in_q[at], status = (pkt >> 8) & 0xFFu;
        uint32_t d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu, ms = midi_in_ms[at];
        mi_r++;
        if (status >= 0xF8u) {
            if (!cr_cin_mode)
                continue;
            if (status == 0xF8u) {
                int b = crm_clock_pulse(&cr_cin, ms);
                if (b) {
                    cr_in_bpm = (uint16_t)b;
                    cr_in_bpm_n++;
                }
                if (cr_cin.bpm && cr.bpm != cr_cin.bpm) {    /* (a SELECT turn meanwhile: the clock wins) */
                    cr_set_tempo(&cr, cr_cin.bpm);
                    cr_out_bpm = cr_cin.bpm;
                    if (!b)
                        cr_in_bpm_n++;
                }
            } else if (status == 0xFAu || status == 0xFBu || status == 0xFCu) {
                cr_in_transport(status);
            }
        } else if (status >= 0x80u && status < 0xF0u) {
            crm_act_t a[2];
            int i, n = crm_map(status, d1, d2, en, ch, a);
            for (i = 0; i < n; i++) {
                if (a[i].kind == CRM_FORWARD)
                    midi_event(status & 0xF0u, a[i].part, d1, d2);
                else
                    cr_min_post(&a[i]);
            }
        }
    }
}

/* ------------------------------------------------------- the audio side --- */
static void cr_out_init(void)                    /* power-on (the audio ISR may already run: IRQ off) */
{
    fm1_irq_off();
    cr_ready = 0;
    cr_init(&cr, &CR_OUT);
    cr_seed(&cr, 0x43524631u);
    voice_fade_steal = 1;                        /* voice.c: a stolen voice fades out, its new note waits a block */
    fx_smooth = 1;                               /* fx.c: the limiter's gain eased (no audio-rate crackle), the delay's
                                                  * new times crossfaded (no click on a tempo change) */
    cr_loop_init(&crl);
    crl_stage_busy = 0;
    cr_now_ms = cr_ms_rem = cr_clk_acc = 0;
    cr_evq_r = cr_evq_w = 0;
    cr_ready = 1;
    fm1_irq_on();
}

/* MIDI start / stop with the loop (Options > MIDI Clock = Out): 0xFA when it starts playing (LOOP from stopped, a
 * take committed into playback), 0xFC when it stops; the UI traces them (cr_rt_n, cr_rt_last) */
static uint8_t cr_rt_was;
static volatile uint32_t cr_rt_n;
static volatile uint8_t cr_rt_last;

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
    cr_midi_in();
    cr_ms_rem += n * 1000u;                      /* samples -> ms, the remainder kept (no drift) */
    while (cr_ms_rem >= FS) {
        cr_ms_rem -= FS;
        cr_now_ms++;
    }
    cr_tick(&cr, cr_now_ms);
    cr_loop_tick(&crl, &cr, cr_now_ms);          /* the loop's playback and capture, the click */
    if (crl_stage_busy == 2u && !crl.next_on)
        crl_stage_busy = 0;
    if ((crl.state == CRL_PLAYING) != cr_rt_was) {
        cr_rt_was = crl.state == CRL_PLAYING;
        if (cr_route.clock_out && !cr_route.clock_in) {   /* USB MIDI CIN 0x0F: a single-byte system real-time message */
            cr_rt_last = cr_rt_was ? 0xFAu : 0xFCu;
            if (cr_rt_was)
                cr_clk_acc = 0;                  /* the pulses start with it */
            midi_out_event(0x0Fu | (uint32_t)cr_rt_last << 8);
            cr_rt_n++;
        }
    }
    if (cr_route.clock_out && !cr_route.clock_in) {   /* 24 PPQN: a pulse every FS * 60 / (bpm * 24) samples */
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
    uint8_t lnote[16];               /* bit n: a loop voice sounds note n (PLAN 6: their keys glow dim) */
    uint8_t ldisp;                   /* the chord shown (ci) is a loop voice's, not the player's */
    /* the PLAY menu's timeline and step grid (cr_ui.c cu_lm_*): the events themselves are read (IRQ off) only when
     * lgen changes */
    uint8_t lrep, lrep_on, lstep_g;  /* the overdub replaces; a gesture of it held; the step grid (ticks) */
    uint16_t lgen, lev0, lstep, lstep_end;   /* the events' generation; the take's first event; the cursor; steps used */
    uint32_t lpos, lrep0, lrec_len, lstep_len;   /* playhead (ticks), the held span's start, the synced take, the steps' */
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
    cr_snap.lrep = crl.rep;
    cr_snap.lrep_on = (uint8_t)(crl.rep && crl.rep_n && crl.cap == CRL_CAP_OD);
    cr_snap.lstep_g = crl.step_g;
    cr_snap.lgen = crl.gen;
    cr_snap.lev0 = crl.layer_ev0;
    cr_snap.lstep = crl.step;
    cr_snap.lstep_end = crl.step_end;
    cr_snap.lpos = cr_loop_pos(&crl);
    cr_snap.lrep0 = crl.rep0;
    cr_snap.lrec_len = crl.rec_len;
    cr_snap.lstep_len = crl.step_len;
    for (n = 0; n < 16u; n++)
        cr_snap.lnote[n] = 0;
    for (n = 0; n < (uint32_t)CR_MAX_LOOPV; n++) {    /* the 8 loop voices' voiced notes */
        const cr_voice_t *v = &cr.v[CR_MAX_VOICES + CR_MAX_PADS + n];
        uint32_t i;
        if (v->used)
            for (i = 0; i < v->nnotes; i++)
                cr_snap.lnote[(v->notes[i] >> 3) & 15u] |= (uint8_t)(1u << (v->notes[i] & 7u));
    }
    cr_snap.ldisp = (uint8_t)(cr.disp_vi >= CR_MAX_VOICES + CR_MAX_PADS && cr.disp_vi < CR_NVOICE);
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
