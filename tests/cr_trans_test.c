/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of TRANSPOSE (firmware/src/cr_out.c: the part's P_TRANS applied where the engine's streams enter the
 * parts; docs/VA.md, docs/INTEGRATION.md): the sound side through tests/hostsim.c, the ChoralRoot engine and
 * cr_out.c as the emulator includes them; the stream callbacks driven directly.
 *   sh tests/run_cr_tests.sh
 * Checks: +12 / -24 / 0 move the part's voices, clamped to 0..127; the MIDI out is not transposed; a P_TRANS change
 * while notes sound keeps them (their note-offs end them) and applies to the next chord; panic; the other part keeps
 * its own; a re-routed note still ends. */
#define FELUCCA_VA 1
#define FELUCCA_SLICE 0
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

int main(void)
{
    track_t *c = &trk[CR_PART_CHORD], *b = &trk[CR_PART_BASS];
    host_tracks_init();
    usb.config = 1;                              /* (the MIDI out queue takes events) */
    host_preset(c, 0, 0);
    host_preset(b, 0, 0);
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
    printf("cr_trans_test: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
