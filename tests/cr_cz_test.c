/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of ChoralRoot's CZ-1 (Melodee's engine, docs/CZ1.md) on the firmware as the emulator builds it
 * (tools/emu/emu_firmware.h: hostsim's sound side, the ChoralRoot UI, storage.c on emu_hal_fw.h's RAM NOR):
 *   sh tests/run_cr_tests.sh
 * Checks: the engine's place (14, after the VA in the picker order), Casio's 64 tones (valid, A-1 BRASS 1 .. H-8
 * TYPHOON, the PRESETS names); every factory tone through the 144-byte blob and back, a bad blob = the init tone;
 * every factory tone rendered (three notes, velocity 127 and 1, held then released) without overflow and not
 * silent; the level (0.19: every sample of a part's render exactly half of 778a6ad's); the pops (a sounding note
 * struck again goes on from its level, at another velocity too; the DCW depth is ramped across a block: no
 * step of the sample at a note-on or at a block's edge, docs/CZ1.md "Pops"); Casio SysEx: a 144-byte tone dump (7n 30) -> the part's tone -> the dump cz_store.c sends on a send
 * request (7n 11 60) identical; a 128-byte (CZ-101) dump gets the CZ-1 defaults; a bad dump is refused; the banks
 * (A..D Casio's, E..H empty; BANK / PTCH load their tone; a bank exported and imported through the backup's objects
 * 14..21); the deep page table (37 pages: titles, sections, every column's get / set at its min / max / default, the
 * tone bytes it is); the tone store (a user slot keeps its tone in two objects, loads it back, survives a reboot, an
 * erase clears it, a slot of no tone loads its BANK / PTCH); the backup's CZ-1 objects (12, 13, 14..21). */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../tools/emu/emu_hooks.h"

emu_hal_t emu_hal;
const int8_t emu_keymap[6][EMU_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};

/* the frames cz_store.c sends (the device: ota_wire_send) */
static uint8_t sx_out[600];
static uint32_t sx_out_n, sx_out_count;
#define CZ_SEND(p, n) (memcpy(sx_out, (p), (n)), sx_out_n = (n), sx_out_count++)
#include "../tools/emu/emu_firmware.h"
#include "cr_pool_shim.h"                       /* (0.22: the patches in the sound pool) */

#ifndef CZ_VOICE_GAIN
#define CZ_VOICE_GAIN 4                              /* (this file in a tree before 0.19's trim: the constants below) */
#endif
/* 778a6ad's hashes of the part's render ("level" in main): INIT TONE and the 64, then by the tone's kind */
#define CZ_REL_ALL 0x129ea1922121e5c9ull
#define CZ_REL_ONE 0x3c6b4d6611b03c4aull
#define CZ_REL_SUM 0x74fea5c4424601beull
#define CZ_REL_RING 0x82aff32eeb2e1618ull
#define CZ_REL_NOISE 0xbe66150f3519f1b6ull

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
#define OK(c, ...)                                                      \
    do {                                                                \
        int ok_ = (c);                                                  \
        CHECK(ok_, __VA_ARGS__);                                        \
        if (ok_) {                                                      \
            printf("ok   ");                                            \
            printf(__VA_ARGS__);                                        \
            printf("\n");                                               \
        }                                                               \
    } while (0)

static void feed(const uint8_t *b, uint32_t n)       /* a frame through the USB collector, then the main loop */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        cz_sx_byte(b[i]);
    cz_service();
}

static uint32_t dump(const uint8_t *raw, uint32_t nb, uint8_t *f)   /* F0 44 00 00 70 30 <2 nb nibbles> F7 */
{
    static const uint8_t H[6] = {0xF0, 0x44, 0x00, 0x00, 0x70, 0x30};
    uint32_t j;
    memcpy(f, H, 6);
    for (j = 0; j < nb; j++) {
        f[6 + 2 * j] = raw[j] & 15u;
        f[7 + 2 * j] = raw[j] >> 4;
    }
    f[6 + 2 * nb] = 0xF7;
    return 2 * nb + 7;
}

/* tone k rendered on its own (cz_native_render): the largest |sample| of three notes held 1 s, released 1 s */
static int32_t render_peak(track_t *t, uint32_t vel)
{
    static const uint8_t N[3] = {36, 60, 84};
    vmod_t m;
    int32_t out[CTL], peak = 0;
    uint32_t k, b, i;
    memset(t->v, 0, sizeof t->v);
    memset(&m, 0, sizeof m);
    m.amp0 = m.amp1 = 32767;
    m.shape = 64 << 8;                               /* (the neutral SHAPE: cz_native_render adds cutoff + shape - 64) */
    for (k = 0; k < 3u; k++) {
        t->v[k].note = N[k];
        t->v[k].vel = (uint8_t)vel;
        t->v[k].gate = 1;
        t->v[k].active = 1;
        voice_was = 0;
        ENG_CZ.note_on(t, &t->v[k]);
    }
    for (b = 0; b < 2u * FS / CTL; b++) {
        memset(out, 0, sizeof out);
        for (k = 0; k < 3u; k++) {
            if (b == FS / CTL)
                t->v[k].gate = 0;
            m.pitch16 = (int32_t)N[k] * 16;
            m.inc = pitch_inc((uint32_t)m.pitch16);
            ENG_CZ.render(t, &t->v[k], out, CTL, &m);
        }
        for (i = 0; i < CTL; i++)
            if ((out[i] < 0 ? -out[i] : out[i]) > peak)
                peak = out[i] < 0 ? -out[i] : out[i];
    }
    memset(t->v, 0, sizeof t->v);
    return peak;
}

/* The part's own render (voice.c track_render: the sum of its voices before LEVEL) of tone k, one note through
 * trk_note_on, for the pops' checks (docs/CZ1.md "Pops"). pop_render: nb blocks more; the largest sample-to-sample
 * step at a block's first sample (*edge: from the block before) and inside the blocks (*in), the largest |sample| of
 * the first 16 samples (*head) and of all (returned). */
static int32_t pop_last;
static void pop_start(track_t *t, uint32_t k)
{
    memset(t->v, 0, sizeof t->v);
    t->engine = t->eng_req = ENGI_CZ;
    t->p[P_VOICE] = V_POLY;
    t->p[P_GLIDE] = 0;
    cz_blob_preset(t, k);
    pop_last = 0;
}
static int32_t pop_render(track_t *t, uint32_t nb, int32_t *edge, int32_t *in, int32_t *head)
{
    int32_t out[CTL], pk = 0, d;
    uint32_t b, i;
    *edge = *in = *head = 0;
    for (b = 0; b < nb; b++) {
        track_render(t, out, CTL);
        for (i = 0; i < CTL; i++) {
            d = out[i] - (i ? out[i - 1u] : pop_last);
            d = d < 0 ? -d : d;
            if (!i)
                *edge = d > *edge ? d : *edge;
            else
                *in = d > *in ? d : *in;
            d = out[i] < 0 ? -out[i] : out[i];
            pk = d > pk ? d : pk;
            if (!b && i < 16u)
                *head = d > *head ? d : *head;
        }
        pop_last = out[CTL - 1u];
    }
    return pk;
}

int main(void)
{
    static uint8_t frame[600];
    uint8_t raw[CZ_BYTES], raw2[CZ_BYTES], b[CZ_BYTES], b2[CZ_BYTES];
    track_t *t = &trk[0];
    uint32_t k, i, bad;

    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();

    /* ---- the engine and its tones ---- */
    for (k = 0, bad = 1; k < NENG_SHOWN; k++)
        if (ENGINE_ORDER[k] == ENGI_VA)
            bad = !(k + 1u < NENG_SHOWN && ENGINE_ORDER[k + 1u] == ENGI_CZ);
    OK(ENGI_CZ == 14u && NENGINES == 15u + FELUCCA_QUAD + FELUCCA_PROPHET && ENGINES[ENGI_CZ] == &ENG_CZ && !strcmp(ENG_CZ.name, "CZ-1") && eng_ok(ENGI_CZ) &&
       !bad, "engine: CZ-1 is 14 (after VA's 13; Melodee: 15), shown after the VA (0.14)");
    for (k = 0, bad = 0; k < CZ_FACTORY_N; k++)
        bad += !cz_patch_valid(CZ_FACTORY[k]);
    {   /* the packed bank (3y129): every tone unpacked (cz_factory_bank, cz_factory_tone) is the tone as it is */
        uint8_t bank[16u * CZ_BYTES], one[CZ_BYTES];
        uint32_t j, n = 0;
        for (j = 0; j < CZ_FACTORY_BANKS; j++) {
            cz_factory_bank(j, bank);
            for (k = 0; k < 16u; k++)
                n += !memcmp(bank + k * CZ_BYTES, CZ_FACTORY[j * 16u + k], CZ_BYTES);
        }
        for (k = 1; k <= CZ_FACTORY_N; k++) {
            cz_factory_tone(k, one);
            n += !memcmp(one, CZ_FACTORY[k - 1u], CZ_BYTES);
        }
        OK(n == 2u * CZ_FACTORY_N, "the packed bank: %u of %u tones unpack to Casio's bytes", n, 2u * CZ_FACTORY_N);
    }
    OK(CZ_FACTORY_N == 64u && !bad && NELEM(CZ_PRESETS) == 65u && !strcmp(CZ_PRESETS[0].name, "INIT TONE") &&
       !strcmp(CZ_PRESETS[1].name, "BRASS 1") && !strcmp(CZ_PRESETS[64].name, "TYPHOON") &&
       !memcmp(CZ_FACTORY[0] + 128, "    BRASS 1     ", 16) && CZ_PRESETS[25].e[0] == 1 && CZ_PRESETS[25].e[1] == 9,
       "tones: Casio's 64 (A-1 BRASS 1 .. H-8 TYPHOON) valid; PRESETS = INIT TONE + the 64, BANK / PTCH A..D 1..16");

    /* ---- the blob ---- */
    t->eng_req = ENGI_CZ;
    for (k = 0, bad = 0; k <= CZ_FACTORY_N; k++) {
        cz_blob_preset(t, k);
        cz_blob_get(t, b);
        cz_factory_tone(k, raw);
        bad += memcmp(b, raw, CZ_BYTES) != 0;
        cz_blob_preset(t, (k + 7u) % 65u);
        cz_blob_set(t, b);
        bad += memcmp(cz_patch[0].raw, raw, CZ_BYTES) != 0;
    }
    OK(!bad && ENG_CZ.deep->blob_size == 144u && ENG_CZ.deep->blob_size <= ENG_BLOB_MAX,
       "blob: the 144-byte tone (128 synthesis bytes + the LCD name): INIT TONE and the 64 round trip");
    memcpy(b2, b, CZ_BYTES);
    b2[3] = 48;                                      /* a detune past 47 */
    cz_blob_set(t, b2);
    cz_patch_init(raw);
    OK(!memcmp(cz_patch[0].raw, raw, CZ_BYTES), "blob: a bad tone -> the init tone");

    /* ---- the carrier table is the cosine the renderer read per sample ---- */
    for (k = 0, bad = 0; k < 2048u; k++)
        bad += CZ_CARRIER[k] != (pd_cos((k << 5) + (k >> 6)) + 32767) >> 1;
    OK(!bad && NELEM(CZ_CARRIER) == 2048u, "carrier: CZ_CARRIER's 2048 phases are (pd_cos + 32767) >> 1, each");

    /* ---- the slopes' table is the two divides of cz_native_pd ---- */
    for (k = 1, bad = 0; k < 2048u; k++)
        bad += CZ_RECIP[k] != (1024u << 16) / k;
    OK(!bad && NELEM(CZ_RECIP) == 2048u, "slopes: CZ_RECIP[x] is (1024 << 16) / x for x = 1 .. 2047");

    /* ---- the level (0.19, docs/CZ1.md "Level"): a voice into its part is exactly half of what it was ---- */
    {
        /* INIT TONE and the 64 through the part's own render (track_render: the voices' sum before LEVEL): C4 at
         * velocity 100, E4 at 127 on another voice 30 blocks later, both let go 30 blocks after, 20 blocks more. The
         * hash of TWICE every sample (FNV-1a 64 of its four bytes) is the hash 778a6ad gave of the sample itself
         * (the same lines, built in a tree of that commit: cz_native.c added voice_amp(..) * 4 there): so every
         * sample is exactly half, of every tone, by its kind: one line, two lines summed, ring modulation, NOISE. */
        static const uint64_t WAS[5] = {CZ_REL_ALL, CZ_REL_ONE, CZ_REL_SUM, CZ_REL_RING, CZ_REL_NOISE};
        static const char *const KIND[5] = {"all 65", "one line", "two lines", "ring modulation", "NOISE"};
        uint8_t eng = t->engine, req = t->eng_req;
        uint64_t h[5];
        uint32_t nk[5] = {0, 0, 0, 0, 0}, q, i, bb;
        int32_t out[CTL], pk = 0;
        for (q = 0; q < 5u; q++)
            h[q] = 1469598103934665603ull;
        for (k = 0; k <= CZ_FACTORY_N; k++) {
            uint32_t ls, md, kind;
            pop_start(t, k);
            ls = cz_patch[0].raw[0] & 3u;
            md = (cz_patch[0].raw[15] >> 3) & 7u;
            kind = ls < 2u ? 1u : md == 4u ? 3u : md == 3u ? 4u : 2u;
            nk[0]++;
            nk[kind]++;
            trk_note_on(t, 60, 100);
            for (bb = 0; bb < 80u; bb++) {
                if (bb == 30u)
                    trk_note_on(t, 64, 127);
                if (bb == 60u) {
                    trk_note_off(t, 60);
                    trk_note_off(t, 64);
                }
                track_render(t, out, CTL);
                for (i = 0; i < CTL; i++) {
                    uint32_t x = (uint32_t)(out[i] * (4 / CZ_VOICE_GAIN)), by;
                    pk = abs(out[i]) > pk ? abs(out[i]) : pk;
                    for (by = 0; by < 4u; by++) {
                        h[0] = (h[0] ^ (uint8_t)(x >> (8u * by))) * 1099511628211ull;
                        h[kind] = (h[kind] ^ (uint8_t)(x >> (8u * by))) * 1099511628211ull;
                    }
                }
            }
        }
        if (getenv("CZ_REL_PRINT"))
            printf("level: %016llxull %016llxull %016llxull %016llxull %016llxull\n", (unsigned long long)h[0],
                   (unsigned long long)h[1], (unsigned long long)h[2], (unsigned long long)h[3], (unsigned long long)h[4]);
        for (q = 0; q < 5u; q++)
            OK(h[q] == WAS[q] && nk[q], "level: %s (%u tones): twice every sample of the part's render is 778a6ad's sample "
                                        "(-6.02 dB, a bit's shift)", KIND[q], (unsigned)nk[q]);
        OK(CZ_VOICE_GAIN == 2 && pk > 0, "level: a voice adds voice_amp() * 2, as dsp.c says of a half-scale "
                                                          "engine (the loudest sample of those renders %d)", (int)pk);
        memset(t->v, 0, sizeof t->v);
        t->engine = eng;
        t->eng_req = req;
    }

    /* ---- pops (docs/CZ1.md "Pops"): a note struck again goes on from its level; the DCW depth is ramped ---- */
    {
        uint8_t eng = t->engine, req = t->eng_req;
        int32_t edge, in, head, pk0, in0, edge1, in1, head1, worst = 0, wk = 0;
        uint32_t fails0 = 0, low = 0;
        /* A-5 STRINGS 2, C5 held 0.7 s (at its sustain), the same key again (voice.c voice_alloc: the sounding voice
         * is the note's): the first sample after it is no step and the level does not fall away. (The DCA started at
         * 0 again: a step of the sample's size, then the attack from silence.) */
        pop_start(t, 5);
        trk_note_on(t, 72, 100);
        pop_render(t, 600u * FS / (1000u * CTL), &edge, &in, &head);
        pk0 = pop_render(t, 100u * FS / (1000u * CTL), &edge, &in0, &head);
        in0 = edge > in0 ? edge : in0;
        trk_note_on(t, 72, 100);
        head1 = pop_render(t, 3, &edge1, &in1, &head);
        OK(edge1 <= in0 + in0 / 2 && in1 <= in0 + in0 / 2 && head1 * 3 >= pk0,
           "pops: a sounding note struck again: its first sample steps %d (the held note's largest step %d), the next "
           "three blocks (a cycle of C5 and more) peak at %d of %d", (int)edge1, (int)in0, (int)head1, (int)pk0);
        /* .. every tone, let go and struck again in the same block (a chord change's common tone), and 60 ms after
         * (the arp): the step at the new note's first sample is at most 1.5 times the largest of the 100 ms before */
        for (k = 0; k <= CZ_FACTORY_N; k++) {
            uint32_t gap;
            for (gap = 0; gap < 2u; gap++) {
                pop_start(t, k);
                trk_note_on(t, 48, 127);
                pop_render(t, 300u * FS / (1000u * CTL), &edge, &in, &head);
                pop_render(t, 100u * FS / (1000u * CTL), &edge, &in0, &head);
                in0 = edge > in0 ? edge : in0;
                trk_note_off(t, 48);
                if (gap) {
                    pop_render(t, 60u * FS / (1000u * CTL), &edge, &in, &head);
                    in0 = edge > in0 ? edge : in > in0 ? in : in0;
                }
                trk_note_on(t, 48, 127);
                pop_render(t, 1, &edge1, &in1, &head1);
                if (edge1 > in0 + in0 / 2 && edge1 > 100 * CZ_VOICE_GAIN) {
                    fails0++;
                    if (edge1 > worst) {
                        worst = edge1;
                        wk = (int32_t)k;
                    }
                }
            }
        }
        OK(!fails0, "pops: INIT TONE and the 64, C3 let go and struck again at once and 60 ms later: no step at the new "
                    "note's first sample (%u of 130 step; the worst %d, tone %d)", (unsigned)fails0, (int)worst, (int)wk);
        /* B-5 SYNTH.BASS and every other tone at C2, a new note: the first 40 blocks, where the DCW envelope rises a
         * fifth of its range a block. The step at a block's first sample is no larger than the steps inside the
         * blocks. (The depth was a block's: the phase function stepped at the block's edge, the sample with it.) */
        for (k = 0, worst = 0; k <= CZ_FACTORY_N; k++) {
            pop_start(t, k);
            trk_note_on(t, 36, 127);
            pop_render(t, 40, &edge, &in, &head);
            if (edge > 2 * in && edge > 100 * CZ_VOICE_GAIN) {
                low++;
                if (edge > worst) {
                    worst = edge;
                    wk = (int32_t)k;
                }
            }
        }
        OK(!low, "pops: INIT TONE and the 64 at C2, the attack's 40 blocks: no step at a block's edge above twice the "
                 "largest inside the blocks (%u tones step; the worst %d, tone %d)", (unsigned)low, (int)worst, (int)wk);
        /* A sounding note struck again at ANOTHER velocity (0.19's second round): the DCA curve of the same level is
         * another gain at the new velocity, and the engine put it on the block's first sample: a step of the voice's
         * whole sample (STRINGS 1 at its sustain, 127 then 30: a third of its level between two samples). The gain
         * now runs on from where the line's last block ended (cz_voice_t.ag). Every tone, C3 at 127 held 400 ms and
         * struck again at 30, and 30 then 127: the step at the new note's first sample is at most 1.5 times the
         * largest of the 100 ms before, and the step into the block after it (the ramp is a block's) at most 1.5
         * times the largest inside those two blocks. */
        {
            uint32_t vfail = 0, dir;
            int32_t e2, i2, h2;
            for (k = 0, worst = 0; k <= CZ_FACTORY_N; k++)
                for (dir = 0; dir < 2u; dir++) {
                    pop_start(t, k);
                    trk_note_on(t, 48, dir ? 30 : 127);
                    pop_render(t, 300u * FS / (1000u * CTL), &edge, &in, &head);
                    pop_render(t, 100u * FS / (1000u * CTL), &edge, &in0, &head);
                    in0 = edge > in0 ? edge : in0;
                    if (dir)                         /* (struck harder: the level it goes to has larger steps of its own) */
                        in0 = in0 * 127 / 30;
                    trk_note_on(t, 48, dir ? 127 : 30);
                    pop_render(t, 1, &edge1, &in1, &head1);
                    pop_render(t, 1, &e2, &i2, &h2);
                    i2 = i2 > in1 ? i2 : in1;        /* (the two blocks' own steps: the attack the new note makes) */
                    i2 = i2 > in0 ? i2 : in0;
                    if ((edge1 > in0 + in0 / 2 && edge1 > 100 * CZ_VOICE_GAIN) || (e2 > i2 + i2 / 2 && e2 > 100 * CZ_VOICE_GAIN)) {
                        vfail++;
                        if (edge1 > worst) {
                            worst = edge1;
                            wk = (int32_t)k;
                        }
                    }
                }
            OK(!vfail, "pops: INIT TONE and the 64, C3 struck again at another velocity while it sounds (127 -> 30, 30 -> "
                       "127): no step at the new note's first sample or after its first block (%u of 130 step; the worst "
                       "%d, tone %d)", (unsigned)vfail, (int)worst, (int)wk);
        }
        {   /* .. and the gain itself: a voice's line ends a block at the DCA curve of its level and velocity, and the
             * next block starts there, whatever the velocity is by then (cz_native_render through the probe's eyes:
             * two blocks of INIT TONE's voice, the velocity changed between them; the first block's samples are the
             * ones the engine made before, bit for bit: one curve a block, not two) */
            vmod_t m;
            voice_t *v = &t->v[0];
            cz_voice_t *c;
            int32_t o1[CTL], o2[CTL], want;
            uint32_t i;
            pop_start(t, 0);
            memset(&m, 0, sizeof m);
            m.amp0 = m.amp1 = 32767;
            m.shape = 64 << 8;
            m.pitch16 = 60 * 16;
            m.inc = pitch_inc(60u * 16u);
            v->note = 60;
            v->vel = 127;
            v->gate = v->active = 1;
            voice_was = 0;
            ENG_CZ.note_on(t, v);
            c = cz_voice(t, v);
            for (i = 0; i < 40u; i++) {
                memset(o1, 0, sizeof o1);
                ENG_CZ.render(t, v, o1, CTL, &m);
            }
            want = cz_native_amp(c->eg[0][2].level, cz_patch[0].raw[16] >> 4, 15u - (cz_patch[0].raw[20] >> 4), 127);
            bad = c->ag[0] != want + 1;
            v->vel = 20;
            voice_was = 2;
            ENG_CZ.note_on(t, v);
            bad += c->ag[0] != want + 1;             /* (kept across the note-on of a sounding voice) */
            memset(o2, 0, sizeof o2);
            ENG_CZ.render(t, v, o2, CTL, &m);
            want = cz_native_amp(c->eg[0][2].level, cz_patch[0].raw[16] >> 4, 15u - (cz_patch[0].raw[20] >> 4), 20);
            bad += c->ag[0] != want + 1;
            voice_was = 0;
            ENG_CZ.note_on(t, v);
            bad += c->ag[0] != 0;                    /* (a free voice: none) */
            OK(!bad, "pops: a line's gain at a block's end is kept for the next block's start (a sounding voice's note-on "
                     "keeps it, a free voice's clears it)");
        }
        {   /* cz_native_pd: the ramp starts at the last block's depth and ends at this block's; none when it is the same */
            cz_hw_pd_t pd;
            uint32_t pk[3] = {0, 0, 0};
            cz_native_pd(&pd, 0, 300, pk, CTL);
            bad = pd.dstep != 0 || pd.pivot != 724u || pd.k0 != CZ_RECIP[724] || pd.k1 != CZ_RECIP[1324];
            cz_native_pd(&pd, 0, 500, pk, CTL);
            bad += pd.dq != 300u << 16 || pd.dq + pd.dstep * CTL != 500u << 16 || pd.pivot != 524u;
            cz_native_pd(&pd, 0, 100, pk, CTL);
            bad += pd.dq != 500u << 16 || pd.dq + pd.dstep * CTL != 100u << 16;
            cz_native_pd(&pd, 0, 100, pk, CTL);
            bad += pd.dstep != 0;
            OK(!bad, "pops: cz_native_pd ramps the depth from the last block's to this block's (300 -> 500 -> 100), none "
                     "on a new voice or when it did not change");
        }
        memset(t->v, 0, sizeof t->v);
        t->engine = eng;
        t->eng_req = req;
    }

    /* ---- every factory tone renders ---- */
    {
        int32_t pk, mx = 0, mn = 0x7FFFFFFF;
        uint32_t silent = 0;
        for (k = 1, bad = 0; k <= CZ_FACTORY_N; k++) {
            cz_blob_preset(t, k);
            pk = render_peak(t, 127);
            bad += pk >= 4 * VOICE_FS * 3;           /* (three voices at most at full scale each) */
            silent += pk < 64;
            mx = pk > mx ? pk : mx;
            mn = pk < mn ? pk : mn;
            pk = render_peak(t, 1);
            bad += pk >= 4 * VOICE_FS * 3;
        }
        OK(!bad && !silent, "render: the 64 tones (3 notes, velocity 127 / 1, held 1 s, released 1 s) within range, none "
                            "silent (peaks %d .. %d of %d)", (int)mn, (int)mx, 4 * VOICE_FS * 3);
    }

    /* ---- Casio SysEx ---- */
    cu.page = PG_NONE;
    trk[0].eng_req = ENGI_CZ;
    trk[1].eng_req = 0;
    memcpy(raw, CZ_FACTORY[24], CZ_BYTES);           /* D-1 PIANO 1 */
    raw[3] = 7;                                      /* (a tone of its own: detune 7) */
    memcpy(raw + 128, " SYSEX  TEST    ", 16);
    k = dump(raw, CZ_BYTES, frame);
    feed(frame, k);
    OK(!memcmp(cz_patch[0].raw, raw, CZ_BYTES) && t->p[P_E1] == 0 && psnd[0].edited,
       "SysEx in: a CZ-1 tone dump (F0 44 00 00 70 30, 288 nibbles) -> the chord part's tone, as sent (edited)");
    {
        static const uint8_t R[8] = {0xF0, 0x44, 0x00, 0x00, 0x70, 0x11, 0x60, 0xF7};
        sx_out_count = 0;
        feed(R, 8);
        OK(sx_out_count == 1u && sx_out_n == k && !memcmp(sx_out, frame, k),
           "SysEx out: a send request (70 11 60) -> the part's tone dump, the frame received byte for byte");
        memcpy(b, sx_out, sx_out_n);
    }
    {
        static const uint8_t R[8] = {0xF0, 0x44, 0x00, 0x00, 0x70, 0x10, 0x60, 0xF7};
        uint8_t f128[300];
        feed(R, 8);                                  /* the 128-byte (CZ-101) dump */
        memcpy(f128, sx_out, sx_out_n);
        OK(sx_out_n == 263u && f128[6 + 2 * 3] == 7u, "SysEx out: a CZ-101 send request (70 10 60) -> a 128-byte dump");
        cz_blob_preset(t, 1);
        feed(f128, 263);
        OK(cz_patch[0].raw[3] == 7u && !memcmp(cz_patch[0].raw + 128, CZ_FACTORY[0] + 128, 16) &&
           (cz_patch[0].raw[20] >> 4) == 15u && cz_patch_valid(cz_patch[0].raw),
           "SysEx in: a 128-byte dump -> the tone, the CZ-1 velocity defaults (the name the part had)");
    }
    memcpy(raw2, cz_patch[0].raw, CZ_BYTES);
    frame[6 + 2 * 3] = 0x0F;                         /* detune 63: out of range */
    frame[7 + 2 * 3] = 0x03;
    feed(frame, k);
    OK(!memcmp(cz_patch[0].raw, raw2, CZ_BYTES), "SysEx in: an invalid tone is refused (the part keeps its tone)");
    trk[0].eng_req = 0;
    feed(b, k);
    OK(!memcmp(cz_patch[0].raw, raw2, CZ_BYTES), "SysEx in: no CZ-1 part: nothing changes");

    /* ---- the banks ---- */
    {
        cz_bank_t *bk;
        int16_t e0, e1;
        cu_load(t, ENGI_CZ, 0, 0);                   /* INIT TONE */
        t->engine = ENGI_CZ;
        t->p[P_E0] = 1;                              /* BANK B, PTCH 9: D-1 PIANO 1 */
        t->p[P_E1] = 9;
        cz_bank_poll();
        OK(!memcmp(cz_patch[0].raw, CZ_FACTORY[16 + 8], CZ_BYTES), "bank: BANK B PTCH 9 -> D-1 (BANK A..D: Casio's)");
        OK(cz_bank_get(4, 0, raw) != 0 && cz_bank_get(7, 15, raw) != 0, "bank: E..H start empty");
        t->p[P_E0] = 4;
        t->p[P_E1] = 1;
        cz_bank_poll();
        OK(!memcmp(cz_patch[0].raw, CZ_FACTORY[16 + 8], CZ_BYTES), "bank: an empty slot keeps the tone");
        /* a bank out (the backup's GET of object 14 + k reads this) and in (PUT, commit) */
        bk = cz_bank_load(0);
        memcpy(cu_loop_buf, bk, sizeof *bk);
        {
            cz_bank_t *nb = (cz_bank_t *)cu_loop_buf;
            memcpy(nb->name, "MY BANK E", 10);
            memcpy(nb->tone[0].raw, raw2, CZ_BYTES);
            nb->used = 0x0003u;
        }
        crb.put = 1;
        crb.id = 18;                                 /* bank E */
        crb.len = sizeof(cz_bank_t);
        crb.crc = st_crc32(cu_loop_buf, sizeof(cz_bank_t));
        crb.pos = crb.len;
        OK(crb_size_ok(18, sizeof(cz_bank_t)) && crb_commit() == 0, "bank: a bank restored as backup object 18 (bank E)");
        cz_bank_poll();
        OK(!memcmp(cz_patch[0].raw, raw2, CZ_BYTES) && cz_bank_get(4, 1, raw) == 0 && !memcmp(raw, CZ_FACTORY[1], CZ_BYTES),
           "bank: a part on BANK E PTCH 1 reloads the restored tone; E2 holds A-2");
        cz_bank_cached = 255;
        OK(cz_bank_load(4) && cz_bank_saved && !memcmp(cz_bank_cache.name, "MY BANK E", 9),
           "bank: bank E from flash (its own name)");
        {
            st_hdr_t h;
            OK(crb_current(18, &h) >= 0 && h.len == sizeof(cz_bank_t) && crb_current(14, &h) < 0 &&
               st_sector(OBJ_CZBANK0 + 4, 0) == 0xAA000u && st_sector(OBJ_CZBANK0 + 7, 1) == 0xB1000u,
               "bank: object 18 at 0xAA000 (banks 0xA2000..0xB1FFF), bank A never saved: no object");
        }
        e0 = t->p[P_E0];
        e1 = t->p[P_E1];
        (void)e0;
        (void)e1;
    }

    /* ---- the deep pages ---- */
    {
        const eng_deep_t *d = ENGINES[ENGI_CZ]->deep;
        static const char *const T[] = {"LINE 1", "LINE 2", "DETUNE", "DCW", "DCW+", "PITCH 1", "PITCH 1+", "PITCH 1 B",
                                        "PITCH 1 B+", "PITCH 1 S", "DCW 1", "DCA 1 S", "PITCH 2", "DCA 2 S", "VIB", "TONE"};
        static const uint8_t TI[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 19, 20, 34, 35, 36};
        uint32_t pg, c, cols = 0;
        for (k = 0, bad = 0; k < NELEM(T); k++)
            bad += strcmp(d->pages[TI[k]].title, T[k]) != 0;
        OK(d && d->npages == 37u && !bad && d->section[0] == 0 && d->section[1] == 3 && d->section[2] == 5 &&
           d->section[3] == 35 && d->section[4] == 36 && d->section[5] == 0xFF && d->mod_dst(t, 0, 0) == -1,
           "deep: 37 pages, sections OSC 0 (LINE n, DETUNE) FILTER 3 (DCW) ENV 5 (PITCH / DCW / DCA n: 5 pages) LFO 35 "
           "(VIB) MOD 36 (TONE); no matrix");
        for (pg = 0, bad = 0; pg < d->npages; pg++)
            for (c = 0; c < 4u; c++) {
                const param_desc_t *p = &d->pages[pg].col[c];
                int32_t vals[3], id = cz_dref(pg, c);
                if (!p->label) {
                    bad += id >= 0;
                    continue;
                }
                cols++;
                bad += id < 0 || p->min < CZ_PD[id].min || p->max > LCZ_MAX[id];
                vals[0] = p->min;
                vals[1] = p->max;
                vals[2] = p->def;
                for (i = 0; i < 3u; i++) {
                    uint8_t pv[LCZ_PACKED];
                    int32_t want = vals[i], got;
                    cz_blob_preset(t, 1);            /* (BRASS 1: SUS before END everywhere it has one) */
                    d->set(t, pg, c, vals[i]);
                    got = d->get(t, pg, c);
                    cz_ed_decode(pv, cz_patch[0].raw);
                    if (!strcmp(p->label, "SUS") || (p->label[0] == 'L' && p->label[1] >= '1' && p->label[1] <= '8'))
                        want = got;                  /* (SUS only before END, END's level 0: the CZ's rules, below) */
                    bad += got != want || pv[id] != got || !cz_patch_valid(cz_patch[0].raw);
                    if (got != want)
                        printf("  page %s col %s: set %d got %d\n", d->pages[pg].title, p->label, (int)vals[i], (int)got);
                }
            }
        OK(!bad && cols == 137u, "deep: every column (%u) set to its min, max and default reads it back, the panel value it "
                                 "names (cz_edit.h), the tone valid", (unsigned)cols);
        cz_blob_preset(t, 1);
        memcpy(raw, cz_patch[0].raw, CZ_BYTES);
        d->set(t, 10 + 2, 1, 40);                    /* DCW 1 B: R6 40 */
        for (i = 0, bad = 0; i < CZ_BYTES; i++)
            bad += cz_patch[0].raw[i] != raw[i];
        OK(d->get(t, 12, 1) == 40 && bad == 1u && cz_patch[0].raw[38 + 10] != raw[38 + 10],
           "deep: DCW 1's R6 is the one tone byte it encodes (the others verbatim)");
        d->set(t, 19, 1, 1);                         /* DCA 1 S: END on step 2 */
        OK(d->get(t, 19, 1) == 1 && (d->get(t, 19, 0) == 8 || d->get(t, 19, 0) < 1) && d->get(t, 16, 1) == 0,
           "deep: END on step 2 drops a SUS at or after it, END's level (DCA 1+ L2) reads 0 (the CZ's rules)");
        d->set(t, 36, 1, 1);                         /* TONE MOD: RING */
        d->set(t, 35, 3, 30);                        /* VIB DEPTH 30 */
        d->set(t, 2, 3, 20);                         /* DETUNE FINE 20 */
        OK(((cz_patch[0].raw[15] >> 3) & 7u) == 4u && cz_patch[0].raw[11] == 30u && (cz_patch[0].raw[2] >> 2) == 21u,
           "deep: RING, vibrato depth, fine detune in Casio's encoding (Casio p. 84: FINE 20 is 21)");
    }

    /* ---- the tone store: a user slot keeps its tone ---- */
    {
        uint32_t slot = 4;                           /* U05 */
        cu_load(t, ENGI_CZ, 9, 0);                   /* B-1 ACO.GUITAR */
        OK(!memcmp(cz_patch[0].raw, CZ_FACTORY[8], CZ_BYTES) && t->p[P_E0] == 0 && t->p[P_E1] == 9,
           "load: PRESETS B-1 loads Casio's tone (BANK A PTCH 9)");
        ENGINES[ENGI_CZ]->deep->set(t, 0, 0, 5);     /* LINE 1 WAVE: DBL SINE */
        ENGINES[ENGI_CZ]->deep->set(t, 25, 2, 33);   /* DCW 2 B: R7 */
        memcpy(raw, cz_patch[0].raw, CZ_BYTES);
        k = (uint32_t)up_store(slot, "CZ EDIT");
        OK(k == 0u || k == 3u, "store: U05 saved (a CZ-1 sound)");
        OK(czu_get(slot, b2) == 0 && !memcmp(raw, b2, CZ_BYTES) && pool_used_of(ENGI_CZ, 0) == 1u << slot,
           "store: its tone in the pool slot (U05)");
        cu_load(t, ENGI_CZ, 3, 0);
        cu_load_user(t, slot, 0);
        OK(!memcmp(cz_patch[0].raw, raw, CZ_BYTES) && t->eng_req == ENGI_CZ, "store: U05 loads its own tone");
        memset(upn, 0, sizeof upn);
        up_boot();
        OK(czu_get(slot, b2) == 0 && !memcmp(raw, b2, CZ_BYTES), "store: from flash after a reboot");
        OK(up_store(20, "UPPER") == 0 && pool_used_of(ENGI_CZ, 1) == 1u << 4, "store: U21 (the second half of 1..32)");
        cu_load(t, 7, 0, 0);                         /* WHEEL (PHASE until 0.18: retired) */
        up_store(slot, "WHEEL NOW");
        OK(czu_get(slot, b2) != 0, "store: another engine's sound over U05 clears its tone");
        up_put(20, 0);
        OK(czu_get(20, b2) != 0 && !pool_used_of(ENGI_CZ, 1), "store: an erased slot clears its tone");
        cu_load(t, ENGI_CZ, 0, 0);                   /* a slot without a tone: its BANK / PTCH */
        t->p[P_E0] = 3;
        t->p[P_E1] = 16;
        up_store(slot, "NO TONE");
        pool_patch_clear(slot);
        cu_load(t, ENGI_CZ, 5, 0);
        cu_load_user(t, slot, 0);
        OK(!memcmp(cz_patch[0].raw, CZ_FACTORY[63], CZ_BYTES), "store: a slot of no tone loads BANK D PTCH 16 (H-8)");
        {
            uint32_t n = 0;
            for (k = 0; k < CRB_N; k++)
                n += crb_is_czbank(CRB_IDS[k]);
            OK(n == 8u && crb_index(12) < 0 && crb_index(13) < 0 && crb_obj(21) == OBJ_CZBANK0 + 7u &&
               !crb_size_ok(12, 2320u) && !crb_size_ok(13, 2320u) && crb_size_ok(14, sizeof(cz_bank_t)) && sizeof(cz_bank_t) == 2332u,
               "backup: objects 14..21 (the banks, 2332 B) listed; 12, 13 (0.21's tone store) neither listed nor taken (3y231)");
        }
    }

    printf("%s: %d checks, %d failed\n", fails ? "CR CZ TESTS FAILED" : "cr cz tests passed", checks, fails);
    return fails != 0;
}
