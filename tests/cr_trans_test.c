/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of TRANSPOSE (firmware/src/cr_out.c: the part's P_TRANS applied where the engine's streams enter the
 * parts; docs/VA.md, docs/INTEGRATION.md): the sound side through tests/hostsim.c (FELUCCA_SEQ 0), the ChoralRoot engine and
 * cr_out.c as the emulator includes them; the stream callbacks driven directly.
 *   sh tests/run_cr_tests.sh
 * Checks: +12 / -24 / 0 move the part's voices, clamped to 0..127; the MIDI out is not transposed; a P_TRANS change
 * while notes sound keeps them (their note-offs end them) and applies to the next chord; panic; the other part keeps
 * its own; a re-routed note still ends. */
#define FELUCCA_VA 1
#define FELUCCA_SLICE 0
#define FELUCCA_SEQ 0                                /* as choralroot.c: cr_out.c's events_block, no seq.c */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define cr_param_t cr_eparam_t
#include "../firmware/src/cr_engine.h"
#include "../firmware/src/cr_engine.c"
#undef cr_param_t
#include "../firmware/src/cr_loop.h"
#include "../firmware/src/cr_loop.c"
static void fm1_irq_off(void) {}                 /* (the HAL's: no ISR on the host) */
static void fm1_irq_on(void) {}
#include "../firmware/src/cr_out.c"

static int fails, checks;
#define CHECK(c, ...)                                                   \
    do {                                                                \
        checks++;                                                       \
        if (!(c)) {                                                     \
            fails++;                                                    \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);               \
            printf(__VA_ARGS__);                                        \
            printf("\n");                                               \
        }                                                               \
    } while (0)

static int gated(const track_t *t, uint32_t note)   /* a voice of the part holds note */
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && t->v[i].note == note)
            return 1;
    return 0;
}
static uint32_t ngated(const track_t *t)
{
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && t->v[i].gate;
    return n;
}
static uint32_t midi_last_note(void)             /* the latest note-on in the USB MIDI out queue, 0xFF = none */
{
    uint32_t i, n = 0xFFu;
    for (i = mo_r; i != mo_w; i++)
        if ((midi_out_q[i % MQ] & 0x0Fu) == 0x09u)
            n = (midi_out_q[i % MQ] >> 16) & 0x7Fu;
    mo_r = mo_w;
    return n;
}

/* (hostsim.c's host_tracks_init / host_preset are Felucca's renders: FELUCCA_SEQ 0 leaves them out) */
static void t_tracks_init(void)                  /* the defaults, as felucca_init */
{
    uint32_t i, k;
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    for (k = 0; k < NTRK; k++)
        for (i = 0; i < P_E0; i++)
            trk[k].p[i] = TP[i].def;
    song.master_q12 = 4096;
}
static void t_preset(track_t *t, uint32_t e, uint32_t pi)   /* engine e's factory preset pi, switched at once */
{
    const preset_t *p = &ENGINES[e]->presets[pi];
    uint32_t i;
    t->eng_req = t->engine = (uint8_t)e;
    t->preset = (uint8_t)pi;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = p->e[i];
    t->p[P_ATK] = p->env[0];
    t->p[P_DEC] = p->env[1];
    t->p[P_SUS] = p->env[2];
    t->p[P_REL] = p->env[3];
    t->p[P_ED_FLT] = p->fenv;
}

int main(void)
{
    track_t *c = &trk[CR_PART_CHORD], *b = &trk[CR_PART_BASS];
    t_tracks_init();
    usb.config = 1;                              /* (the MIDI out queue takes events) */
    t_preset(c, 2, 0);                           /* PHASE BRASS (ANALOG's SAW LEAD until 0.14: retired, FELUCCA_ANALOG 0) */
    t_preset(b, 2, 0);
    c->p[P_VOICE] = V_POLY;
    b->p[P_VOICE] = V_MONO;
    /* 0: as played */
    cr_cb_note_on(0, CR_STREAM_MAIN, 60, 100);
    CHECK(gated(c, 60) && midi_last_note() == 60u, "TRANS 0: C4 plays C4");
    cr_cb_note_off(0, CR_STREAM_MAIN, 60);
    CHECK(!ngated(c), "TRANS 0: released");
    /* +12: an octave up; MIDI as played */
    c->p[P_TRANS] = 12;
    cr_cb_note_on(0, CR_STREAM_MAIN, 60, 100);
    cr_cb_note_on(0, CR_STREAM_MAIN, 64, 100);
    CHECK(gated(c, 72) && gated(c, 76) && !gated(c, 60), "TRANS +12: C4 E4 play C5 E5");
    CHECK(midi_last_note() == 64u, "TRANS +12: the MIDI out is not transposed");
    /* a change while they sound: they keep theirs, their note-offs end them */
    c->p[P_TRANS] = -5;
    cr_cb_note_on(0, CR_STREAM_MAIN, 67, 100);
    CHECK(gated(c, 79), "TRANS changed while held: the chord keeps +12 (G4 -> G5)");
    cr_cb_note_off(0, CR_STREAM_MAIN, 60);
    cr_cb_note_off(0, CR_STREAM_MAIN, 64);
    cr_cb_note_off(0, CR_STREAM_MAIN, 67);
    CHECK(!ngated(c), "TRANS changed while held: every note-off ended its note (%u held)", ngated(c));
    cr_cb_note_on(0, CR_STREAM_MAIN, 67, 100);
    CHECK(gated(c, 62) && ngated(c) == 1u, "TRANS -5: the next chord: G4 plays D4");
    cr_cb_note_off(0, CR_STREAM_MAIN, 67);
    /* clamped to 0..127 */
    c->p[P_TRANS] = 24;
    cr_cb_note_on(0, CR_STREAM_MAIN, 120, 100);
    CHECK(gated(c, 127), "TRANS +24: note 120 clamps to 127");
    cr_cb_note_off(0, CR_STREAM_MAIN, 120);
    c->p[P_TRANS] = -24;
    cr_cb_note_on(0, CR_STREAM_MAIN, 10, 100);
    CHECK(gated(c, 0), "TRANS -24: note 10 clamps to 0");
    cr_cb_note_off(0, CR_STREAM_MAIN, 10);
    CHECK(!ngated(c), "clamped notes released");
    /* the bass part: its own */
    b->p[P_TRANS] = -12;
    c->p[P_TRANS] = 7;
    cr_cb_note_on(0, CR_STREAM_BASS, 40, 100);
    cr_cb_note_on(0, CR_STREAM_MAIN, 60, 100);
    CHECK(gated(b, 28) && gated(c, 67), "parts: bass -12 (E2 -> E1), chord +7 (C4 -> G4)");
    /* panic: everything off, the next note takes the new value */
    cr_cb_all_off(0, CR_STREAM_MAIN);
    cr_cb_all_off(0, CR_STREAM_BASS);
    CHECK(!ngated(c) && !ngated(b), "panic: all off");
    c->p[P_TRANS] = 2;
    cr_cb_note_on(0, CR_STREAM_MAIN, 60, 100);
    CHECK(gated(c, 62), "after panic: the new TRANS (+2)");
    /* a re-route while held: the note-off still ends the transposed note on its part */
    cr_route.part[CR_STREAM_MAIN] = CR_PART_BASS;
    cr_cb_note_off(0, CR_STREAM_MAIN, 60);
    CHECK(!ngated(c), "re-routed: the chord part's note ended");
    cr_route.part[CR_STREAM_MAIN] = CR_PART_CHORD;

    /* MIDI in on the parts (cr_out.c midi_event, FELUCCA_SEQ 0: what midi_control.c did for ChoralRoot) */
    c->p[P_TRANS] = 0;
    midi_event(0x90u, 0, 60, 90);
    midi_event(0x90u, 0, 64, 90);
    CHECK(gated(c, 60) && gated(c, 64), "MIDI in: note-ons play on the part");
    midi_event(0x80u, 0, 60, 0);
    midi_event(0x90u, 0, 64, 0);
    CHECK(!ngated(c), "MIDI in: note-off and velocity 0 release");
    midi_event(0xB0u, 0, 64, 127);                      /* the pedal down */
    midi_event(0x90u, 0, 62, 90);
    midi_event(0x80u, 0, 62, 0);
    CHECK(gated(c, 62), "MIDI in: the pedal holds a released note");
    midi_event(0xB0u, 0, 64, 0);
    CHECK(!ngated(c), "MIDI in: the pedal up releases it");
    midi_event(0xE0u, 1, 127, 127);                     /* bend up full: +2 semitones (Q8) */
    CHECK(midi_bend_target[1] == 512 && !midi_bend_target[0], "MIDI in: bend on the bass part, +2 semitones (%d)",
          (int)midi_bend_target[1]);
    midi_event(0xB0u, 1, 101, 0); midi_event(0xB0u, 1, 100, 0); midi_event(0xB0u, 1, 6, 12);
    CHECK(midi_bend_target[1] == 12 * 256, "MIDI in: RPN 0 sets the bend range (12 semitones)");
    midi_event(0xB0u, 1, 121, 0);
    CHECK(!midi_bend_target[1], "MIDI in: CC 121 centres the bend");
    midi_event(0xB0u, 0, 1, 100);
    midi_event(0xD0u, 0, 50, 0);
    CHECK(c->mw == 100 && c->at == 50, "MIDI in: the mod wheel and pressure reach mod.c's sources");
    midi_event(0x90u, 0, 48, 90);
    midi_event(0x90u, 0, 52, 90);
    midi_event(0xB0u, 0, 123, 0);
    CHECK(!ngated(c), "MIDI in: CC 123 releases the part's notes");
    midi_event(0x90u, 0, 55, 90);
    panic_req = 1u << CR_PART_CHORD;                    /* a sound load: the part released, its MIDI notes forgotten */
    events_block(CTL);
    midi_event(0x90u, 0, 57, 90);
    midi_event(0x80u, 0, 55, 0);
    CHECK(gated(c, 57) && !gated(c, 55), "panic_req: the part released; a forgotten note's off ends nothing else");
    midi_event(0x80u, 0, 57, 0);
    printf("cr_trans_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
