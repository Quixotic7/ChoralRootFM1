/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The firmware's sound side on the host (engines, voices, FX, usb.c's MIDI queues), the same sources, in ChoralRoot's
 * configuration (core.h's defaults: the VA, CZ-1, FM TONE, P5, FM6, WHEEL, GRAIN; parts 1 and 2 sound): the base of the
 * host tests (tests/regress.c, the cr_*_test.c files) and of the emulator (tools/emu/emu_firmware.h). Felucca's
 * sequencer, its 4-track renders and its WAV tool (this file's main until 3y232) left with Felucca's tree.
 *   HOST_CR_OUT 1 (the emulator, tests/cr_trans_test.c): cr_out.c follows, with its events_block and the names it
 *                 keeps of Felucca's sequencer; this file gives the sound side alone
 *   else          the host stand-ins of those (events_block: the engine switches and the releases asked for, as
 *                 cr_out.c's, without its MIDI channels) and the helpers below: host_tracks_init, host_preset,
 *                 wav_hdr / wav_put, now_ns, busy_now */
#include <stdio.h>
#include <unistd.h>
#include <sys/wait.h>
#include <time.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#define __attribute__(x)
#define memset felucca_memset
#define memcpy felucca_memcpy
#define memcmp felucca_memcmp
#include "felucca_tables.h"
#include "../firmware/src/libc.c"
#undef memset
#undef memcpy
#undef memcmp
static struct { volatile uint32_t notes, buttons; } fm1_in;
#include "../firmware/src/core.h"
#include "../firmware/src/engines.c"
#include "../firmware/src/params.c"
#include "../firmware/src/mod.c"
#include "../firmware/src/voice.c"
#include "../firmware/src/slicer.c"
#if CR_DRUMS
#include "../firmware/src/cr_drum.c"              /* the drums (the emulator: tools/emu/emu_firmware.h sets CR_DRUMS) */
#endif
#include "../firmware/src/fx.c"
static void fm1_delay_ms(uint32_t ms) { (void)ms; }
#include "../firmware/src/usb.c"
#include "../firmware/src/midi_uart.c"                /* TRS MIDI IN: its parser (um_byte) feeds midi_in_q */
#ifndef HOST_CR_OUT
/* ------------------------------------------ cr_out.c's names, on the host --- */
static volatile uint8_t panic_req;               /* bit per part: its notes released (as cr_out.c's) */
static const uint8_t transport_req = 0;          /* (Felucca's transport: gone) */
static uint32_t trk_index(const track_t *t) { return (uint32_t)(t - trk); }
static int drum_track(const track_t *t) { (void)t; return 0; }
static int16_t motion_base_value(const track_t *t, uint32_t id) { return t->p[id]; }
static void midi_event(uint32_t st, uint32_t ch, uint32_t d1, uint32_t d2)   /* (no MIDI channels to the parts here) */
{
    (void)st; (void)ch; (void)d1; (void)d2;
}
/* fx.c's, between two rendered blocks: the releases asked for, the engine switches (cr_out.c's without its MIDI
 * channels; a queue that overflowed is dropped and every part released) */
static void events_block(uint32_t n)
{
    uint32_t i, pr = panic_req;
    (void)n;
    panic_req = 0;
    if (midi_in_overflow) {
        mi_r = mi_w;
        pr |= (1u << NTRK) - 1u;
        midi_in_overflow = 0;
    }
    for (i = 0; i < NTRK; i++) {
        if ((pr >> i) & 1u)
            trk_all_off(&trk[i]);
        engine_block(&trk[i]);
    }
}

/* ------------------------------------------------------------- helpers --- */
static void wav_hdr(FILE *f, uint32_t frames)
{
    uint32_t v;
    fwrite("RIFF", 1, 4, f); v = 36 + frames * 4; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    uint16_t a = 1, ch = 2, ba = 4, bits = 16; uint32_t sr = FS, br = FS * 4;
    fwrite(&a, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&sr, 4, 1, f); fwrite(&br, 4, 1, f);
    fwrite(&ba, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); v = frames * 4; fwrite(&v, 4, 1, f);
}

static void wav_put(FILE *f, int32_t l, int32_t r)
{
    int16_t s[2] = {(int16_t)(l > 32767 ? 32767 : l < -32768 ? -32768 : l),
                    (int16_t)(r > 32767 ? 32767 : r < -32768 ? -32768 : r)};
    fwrite(s, 2, 2, f);
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

static void host_tracks_init(void)                /* as felucca_init: defaults, empty patterns */
{
    uint32_t i, k;
    fm6_tables_init();                            /* FM6's RAM tables (eng_fm6.c fm6_init: FM6_SIN, the log-sine halves) */
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    for (k = 0; k < NTRK; k++) {
        for (i = 0; i < P_E0; i++)
            trk[k].p[i] = TP[i].def;
        for (i = 0; i < NSTEP; i++)
            trk[k].step[i].time = ST_REST;
    }
    song.master_q12 = 4096;
}

/* the factory preset as Felucca's ui.c apply_preset_to set it (sound, sends; not the track's own: the mix, ARP, SCL,
 * the pattern parameters, the SLICER); a retired engine's slot: its empty values;
 * the engine as the UI asks for it (eng_req: the audio side switches; host_preset: at once) */
static void host_preset_values(track_t *t, uint32_t e, uint32_t pi, const preset_t *p)
{
    static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
    uint32_t i;
    t->eng_req = (uint8_t)e;
    t->preset = (uint8_t)pi;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = p->e[i];
    t->p[P_ATK] = p->env[0];
    t->p[P_DEC] = p->env[1];
    t->p[P_SUS] = p->env[2];
    t->p[P_REL] = p->env[3];
    t->p[P_ED_FLT] = p->fenv;
    t->p[P_VOICE] = p->mono ? V_LEGATO : V_POLY;
    for (i = 0; i < 4u; i++)
        t->p[P_DIST + i] = (int16_t)(p->fx[i] ? p->fx[i] - 1 : FX_DEF[i]);
    fm6_track_loaded(t);                          /* FM6: the preset's patch (ui.c apply_preset_to) */
}
static void host_preset_req(track_t *t, uint32_t e, uint32_t pi)
{
    uint32_t n = ENGINES[e]->npresets;
    if (!n) {
        t->eng_req = (uint8_t)e;
        t->preset = 0;
        for (uint32_t i = 0; i < 8u; i++)
            t->p[P_E0 + i] = ENGINES[e]->edit[i].def;
        return;
    }
    pi %= n;
    host_preset_values(t, e, pi, &ENGINES[e]->presets[pi]);
}
static void host_preset(track_t *t, uint32_t e, uint32_t pi)
{
    host_preset_req(t, e, pi);
    t->engine = t->eng_req;
}

static uint32_t busy_now(void)                   /* sounding voices of the parts (none fading after a block) */
{
    uint32_t p, i, n = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++)
            n += trk[p].v[i].active;
    return n;
}

#endif /* !HOST_CR_OUT */
