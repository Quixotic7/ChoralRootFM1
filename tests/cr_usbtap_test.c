/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Does the USB recording change what the instrument itself plays? (the owner, 2026-10-12; docs/USB-AUDIO.md "Does USB
 * recording affect the sound?") The mix on the host as tests/cr_cpu1_test.c builds it (tests/hostsim.c with CR_FX 1,
 * CR_CPU1 1 and FELUCCA_UAC 1: fx.c's capture stage, usb.c's ring and resampler), and around it the lines of
 * choralroot.c's mix_block shim and audio.c's audio_block that touch the capture, copied below (block()).
 * One passage (a loud chord with a chord change over a bass note, then the releases: the limiter works) is rendered
 * in five USB states, and the words the DAC is given are hashed, all 24 bits of them (the Q15 sample x 128 plus
 * Output Clean's seven bits under it):
 *   off        USB Record Off (the console is presented): nothing staged
 *   closed     USB Record On, no program records: nothing staged
 *   waiting    the stream open, the host has not read yet (cap_hold): staged, not fed
 *   44.1 kHz   the stream open and read: staged, fed into the ring
 *   48 kHz     the same with the host at 48 kHz (Options > USB Rate 44.1 + 48 kHz): staged, resampled, fed
 * for Output Clean and Classic, USB Level Master and Fixed, MASTER at full and at -18 dB, one core and the bass on
 * the second, and two sound pairs (with and without sends, so every mix loop of fx.c runs with and without its
 * tap: mix_loop_f / _fu, mix_loop / _u, and the stereo-side ones on VA). The claim: within one setting of Output,
 * USB Level and MASTER the five hashes are equal: the capture is a tap. (USB Level itself is a setting of the
 * output chain: Fixed and Master differ by design below full MASTER; that is checked to be so, and said.)
 *   cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_usbtap_test tests/cr_usbtap_test.c -lm */
#define CR_CPU1 1
#define CR_FX 1                         /* as choralroot.c: ChoralRoot's delay and reverb after the parts (cr_fx.c) */
#define FELUCCA_UAC 1
#ifndef FELUCCA_VA
#define FELUCCA_VA 1
#endif
#ifndef FELUCCA_CZ
#define FELUCCA_CZ 1
#endif
#ifndef FELUCCA_QUAD
#define FELUCCA_QUAD 1
#endif
#ifndef FM6_POLY
#define FM6_POLY 8
#endif
#define main hostsim_main
#include "hostsim.c"
#undef main

static int fails, checks;
static void check(const char *what, int ok)
{
    printf("cr_usbtap: %-108s %s\n", what, ok ? "ok" : "FAIL");
    checks++;
    fails += !ok;
}

#define NBLK 1004u                          /* the passage: 4 + 300 + 300 + 400 blocks of 32 frames (0.73 s) */
enum { U_OFF, U_CLOSED, U_WAITING, U_441, U_48, U_N };
static const char *const U_NAME[U_N] = {"USB Record Off", "On, the stream closed", "open, the host not reading yet",
                                        "open at 44.1 kHz", "open at 48 kHz"};
static uint64_t hash;                               /* the DAC's words */
static uint32_t staged_nz, fed, resampled, cdc;     /* what the capture did meanwhile (the test is not vacuous) */

/* one block as the unit runs it. The lines between the rules are choralroot.c's mix_block shim (the engine's tick and
 * the click left out: no chord engine and no metronome here) and audio.c's audio_block; fx.c's mix_block, out_master,
 * usb_fixed_dac and usb.c's ua_block48 / ua_feed are the firmware's own */
static void block(void)
{
    int32_t out[2 * CTL];
    uint32_t i, n = CTL;
    /* ---- choralroot.c mix_block (the shim) */
    ua_stage_on = ua.cap_alt && !cdc;               /* (USB_CDC_ON: the console is presented, USB Record Off) */
    mix_block(out, n);                              /* fx.c */
    if (ua_stage_on) {
        for (i = 0; i < n; i++) {
            ua_stage[i * UA_CAP_CHANNELS] = ua_sat(out[2u * i]);
            ua_stage[i * UA_CAP_CHANNELS + 1u] = ua_sat(out[2u * i + 1u]);
        }
    }
    /* ---- audio.c audio_block */
    if (fx_usb_fixed)
        usb_fixed_dac(out, n);
    if (ua_stage_on) {
        const int16_t *st = ua_stage;
        uint32_t fn = n, r48 = ua.r48;
        if (r48) {
            fn = ua_block48(ua_stage, n);
            st = ua.rs_out;
            resampled += fn;
        }
        if (usb.up && usb.config && !usb.suspended && !cdc) {
            uint32_t before = ua.cw;
            ua_feed(st, fn, r48);
            fed += ua.cw - before;
            if (ua.cw - ua.cr > 256u)               /* (the host takes its packets: the ring never runs over) */
                ua.cr = ua.cw - 128u;
        }
        for (i = 0; i < n * UA_CAP_CHANNELS; i++)
            staged_nz += ua_stage[i] != 0;
    }
    for (i = 0; i < n; i++) {
        out[2u * i] *= 1 << 7;                      /* OUT_SHIFT: the Q15 sample into the 24-bit word */
        out[2u * i + 1u] *= 1 << 7;
    }
    if (fx_out_blk)
        for (i = 0; i < 2u * n; i++)
            out[i] += out_frac[i & (2u * CTL - 1u)];
    /* ---- */
    for (i = 0; i < 2u * n; i++)
        hash = (hash ^ (uint32_t)out[i]) * 0x100000001B3ull;
}

static void blocks(uint32_t nb)
{
    while (nb--)
        block();
}

struct cfg { uint8_t usb, clean, fixed, core, pair; uint16_t master; };

static void render(const struct cfg *c, uint64_t r[4])
{
    static const uint8_t CH1[4] = {48, 60, 64, 67}, CH2[4] = {50, 62, 65, 69};
    uint32_t i, k;
    host_tracks_init();
    voice_fade_steal = 1;                           /* as ChoralRoot (cr_out.c) */
    if (c->pair == 0) {                             /* CZ-1 chords with sends over a VA bass (the stereo side) */
        host_preset(&trk[0], ENGI_CZ, 0u);
        host_preset(&trk[1], ENGI_VA, 19u);
        trk[0].p[P_CHOR] = 50;
        trk[0].p[P_DLY] = 40;
        trk[0].p[P_REV] = 60;
    } else {                                        /* FM6 chords without sends, a VA bass with them */
        host_preset(&trk[0], ENGI_FM6, 0u);
        host_preset(&trk[1], ENGI_VA, 3u);
        trk[0].p[P_CHOR] = trk[0].p[P_DLY] = trk[0].p[P_REV] = 0;
        trk[1].p[P_REV] = 30;
    }
    for (k = 0; k < 2u; k++) {
        trk[k].p[P_VOICE] = V_POLY;
        trk[k].p[P_LEVEL] = 120;                    /* loud: the limiter (Classic's soft clip, Clean's knee) works */
    }
    cr_dbg.c1 = c->core ? C1_OK : C1_OFF;
    song.master_q12 = c->master;
    fx_usb_fixed = c->fixed;
    fx_output_set(c->clean, 1);
    /* the USB side as usb.c leaves it in each state */
    memset(&ua, 0, sizeof ua);
    ua_reset();
    usb.up = usb.config = 1;
    usb.suspended = 0;
    cdc = c->usb == U_OFF;
    ua.cap_alt = c->usb >= U_WAITING;
    ua.cap_hold = c->usb == U_WAITING;
    ua.r48 = c->usb == U_48;
    hash = 0xCBF29CE484222325ull;
    staged_nz = fed = resampled = 0;
    blocks(4);
    for (i = 0; i < 4u; i++)
        trk_note_on(&trk[0], CH1[i], 127);
    trk_note_on(&trk[1], 36, 127);
    blocks(300);
    for (i = 0; i < 4u; i++) {
        trk_note_off(&trk[0], CH1[i]);
        trk_note_on(&trk[0], CH2[i], 120);
    }
    blocks(300);
    for (i = 0; i < 4u; i++)
        trk_note_off(&trk[0], CH2[i]);
    trk_note_off(&trk[1], 36);
    blocks(400);
    r[0] = hash;
    r[1] = staged_nz;
    r[2] = fed;
    r[3] = resampled;
}

/* in a child: the global state starts fresh for each render (as tests/cr_cpu1_test.c and regress.c run theirs) */
static int fork_render(const struct cfg *c, uint64_t r[4])
{
    int fd[2];
    pid_t pid;
    r[0] = r[1] = r[2] = r[3] = 0;
    if (pipe(fd))
        return 0;
    pid = fork();
    if (!pid) {
        uint64_t v[4];
        render(c, v);
        _exit(write(fd[1], v, sizeof v) == (ssize_t)sizeof v ? 0 : 1);
    }
    close(fd[1]);
    if (read(fd[0], r, 4 * sizeof r[0]) != (ssize_t)(4 * sizeof r[0]))
        r[0] = 0;
    close(fd[0]);
    waitpid(pid, 0, 0);
    return r[0] != 0;
}

int main(void)
{
    static const uint16_t MASTER[2] = {4096, 515};  /* full, -18 dB */
    uint64_t href[2][2][2] = {{{0}}};               /* [clean][fixed][master] of pair 0, one core */
    uint32_t clean, fixed, m, core, pair, u, all_same = 1, combos = 0, renders = 0, tap_seen = 1, rs_seen = 1;
    char s[200];
    printf("cr_usbtap: the DAC's words with and without the USB recording (docs/USB-AUDIO.md)\n");
    for (pair = 0; pair < 2u; pair++)
        for (core = 0; core < 2u; core++)
            for (clean = 0; clean < 2u; clean++)
                for (fixed = 0; fixed < 2u; fixed++)
                    for (m = 0; m < 2u; m++) {
                        uint64_t r[U_N][4];
                        uint32_t same = 1;
                        for (u = 0; u < U_N; u++) {
                            struct cfg c = {(uint8_t)u, (uint8_t)clean, (uint8_t)fixed, (uint8_t)core, (uint8_t)pair,
                                            MASTER[m]};
                            if (!fork_render(&c, r[u]))
                                same = 0;
                            renders++;
                        }
                        for (u = 1; u < U_N; u++)
                            if (r[u][0] != r[0][0])
                                same = 0;
                        /* the capture did its work in the states that have one: staged, fed, resampled */
                        if (r[U_OFF][1] || r[U_CLOSED][1] || !r[U_WAITING][1] || r[U_WAITING][2] || !r[U_441][1] ||
                            r[U_441][2] != NBLK * CTL || r[U_441][3])
                            tap_seen = 0;
                        if (!r[U_48][3] || r[U_48][2] != r[U_48][3] || r[U_48][3] < NBLK * CTL * 160u / 147u - 2u ||
                            r[U_48][3] > NBLK * CTL * 160u / 147u + 2u)
                            rs_seen = 0;
                        if (!same) {
                            all_same = 0;
                            printf("  DIFFERENT: sounds %u, %s, Output %s, USB Level %s, MASTER %u:", pair,
                                   core ? "two cores" : "one core", clean ? "Clean" : "Classic", fixed ? "Fixed" : "Master",
                                   MASTER[m]);
                            for (u = 0; u < U_N; u++)
                                printf(" %016llx", (unsigned long long)r[u][0]);
                            printf("\n");
                        }
                        if (!pair && !core)
                            href[clean][fixed][m] = r[0][0];
                        combos++;
                    }
    snprintf(s, sizeof s, "%u settings x 5 USB states (%u renders of 32128 frames): the DAC's 24-bit words are the same in all five",
             combos, renders);
    check(s, all_same && combos == 32u);
    for (u = 0; u < U_N; u++)
        printf("             state %u: %s\n", u + 1u, U_NAME[u]);
    check("  the capture ran where it should: nothing staged with the stream closed; staged but not fed while the host waits",
          tap_seen);
    check("  .. every frame fed at 44.1 kHz; at 48 kHz 160 frames resampled and fed for every 147", rs_seen);
    /* the test can tell two outputs apart, and USB Level is a setting of the output chain (by design, not the tap's) */
    check("Output Clean and Classic give different words (the hash sees the chain)",
          href[0][0][0] != href[1][0][0] && href[0][0][1] != href[1][0][1]);
    check("USB Level Master and Fixed at full MASTER: the same words, Classic and Clean (MASTER x 1 either side of the limiter)",
          href[0][0][0] == href[0][1][0] && href[1][0][0] == href[1][1][0]);
    printf("             USB Level Master / Fixed at MASTER -18 dB: Classic %s, Clean %s (the setting moves MASTER behind the\n"
           "             limiter: Classic's limiter then works on the full-level mix; Clean's is in front of MASTER either way and\n"
           "             its word may differ by a unit of 24 bits, tests/cr_output_test.c)\n",
           href[0][0][1] == href[0][1][1] ? "the same words" : "different words",
           href[1][0][1] == href[1][1][1] ? "the same words" : "different words");
    check("USB Level Fixed below full MASTER, Classic: another output than Master's (the option's own doing, with or without a host)",
          href[0][0][1] != href[0][1][1]);
    printf("cr_usbtap: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
