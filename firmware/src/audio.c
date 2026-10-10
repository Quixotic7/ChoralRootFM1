/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* I2S output (ALNK0 -> external codec) and the
 * audio ISR: per half buffer, blocks of CTL samples: mix_block (fx.c: events ->
 * each synth part -> dist -> level / pan -> sends -> buses -> master) -> 24-bit stereo. */
/* registers: hal/fm1_audio.h */
#define HALF_WORDS (HALF_FRAMES * 2u)
#define DAC_TICKS 544u            /* TIMER4 ticks per I2S frame (24 MHz / 44,117.6 Hz) */
#define CPU_AVG (4096u / HALF_FRAMES)   /* halves the load meter averages: ~93 ms whatever the half */
#define OUT_SHIFT 7               /* Q15 -> 24-bit, -6 dBFS ceiling (M0f ran clean at -18 dBFS) */

static int32_t abuf[2u * HALF_WORDS] __attribute__((aligned(4)));

/* diagnostics, kept across resets and UBOOT entry: read with `fm1t memr` */
#define DBG_MAGIC 0x44424731u                       /* "DBG1" */
struct felucca_dbg {
    uint32_t magic, halves, max_us, nested, in_audio, late, timer_irqs, ui_frames;
    uint32_t last_us, cpu_q8, boots;
    uint32_t stage, page, home;           /* where the main loop is (breadcrumbs) */
    uint32_t prev_stage, prev_page, prev_home, prev_rst, prev_frames;   /* as found at boot */
} felucca_dbg __attribute__((section(".noinit")));
#define CR_STAGE(n) (felucca_dbg.stage = (n))     /* the boot breadcrumb (cr_bootguard.h BS_*) */
static volatile uint32_t audio_halves, audio_max_us;
static volatile uint32_t t5_nested_ticks;              /* TIMER4 ticks TIMER5 spent nested in this ISR (main.c) */
static uint32_t audio_cpu_rem;                         /* keep the fractional IIR step: no low-load bias */
/* the last second's audio ISR (ChoralRoot's console `cpu`, the GEEK OUT view): the ISR adds each half's render time
 * (us, TIMER5 nested in it left out) and its whole time (ticks, nested included: what the DMA deadline sees);
 * cpu_window() in the main loop closes a window every second (cpu_last) */
static volatile uint32_t aw_max_us, aw_sum_us, aw_n, aw_max_all;
static volatile uint8_t aw_reset;                /* the UI took the window: the ISR starts a new one (lock-free) */
static struct {
    uint32_t avg_us, max_us, max_all_us, halves, late, ui_max_ms, ui_frames;   /* the last full second */
    uint32_t late0, t0, ui_last, ui_max, ui_n;                                 /* the window being filled */
} cpu_last;
static void cpu_window(uint32_t now_ms)          /* once per UI frame: a frame's period, and the second's close */
{
    uint32_t d = now_ms - cpu_last.ui_last;
    if (cpu_last.ui_last && d > cpu_last.ui_max)
        cpu_last.ui_max = d;
    cpu_last.ui_last = now_ms;
    cpu_last.ui_n++;
    if (now_ms - cpu_last.t0 < 1000u)
        return;
    if (aw_reset)
        return;                                  /* (the ISR has not taken the last one yet: no half since) */
    cr_prof_window();                            /* the ISR's stages (cr_cpu1.h cr_dbg) */
    cpu_last.avg_us = aw_n ? aw_sum_us / aw_n : 0u;
    cpu_last.max_us = aw_max_us;
    cpu_last.max_all_us = aw_max_all / FM1_TICKS_PER_US;
    cpu_last.halves = aw_n;
    aw_reset = 1;
    cpu_last.late = felucca_dbg.late - cpu_last.late0;
    cpu_last.late0 = felucca_dbg.late;
    cpu_last.ui_max_ms = cpu_last.ui_max;
    cpu_last.ui_frames = cpu_last.ui_n;
    cpu_last.ui_max = cpu_last.ui_n = 0;
    cpu_last.t0 = now_ms;
}
#define SCOPE_N 512u
static int16_t scope_buf[SCOPE_N];
static uint32_t scope_w;

static void audio_block(int32_t *out, uint32_t n)       /* mix (fx.c), then Q15 -> 24 bit */
{
    uint32_t i;
    mix_block(out, n);
    if (fx_usb_fixed)                                   /* Options > USB Level = Fixed: USB took the full level, the */
        usb_fixed_dac(out, n);                          /* DAC (speaker, headphones) gets MASTER's (fx.c) */
#if FELUCCA_UAC
    /* the USB recording's ring (usb_audio_stream.c): the capture frames in (fx.c ua_stage). TIMER5 serves the
     * endpoint nested in this render (main.c): only this short copy goes without IRQs, stream resets and alternate
     * changes included, not the synth and FX work (Melodee's) */
    if (ua_stage_on) {                                  /* (the computer records: else no IRQ-off section at all) */
        fm1_irq_off();
        if (usb.up && usb.config && !usb.suspended && !USB_CDC_ON)
            ua_audio(ua_stage, n);
        fm1_irq_on();
    }
#endif
    for (i = 0; i < n; i++) {
        if (i & 1u)
            scope_buf[scope_w++ & (SCOPE_N - 1u)] = (int16_t)out[2u * i];
        out[2u * i] *= 1 << OUT_SHIFT;
        out[2u * i + 1u] *= 1 << OUT_SHIFT;
    }
}

/* Overload: two halves in a row above 85 % (the render plus TIMER5 nested in it), fade one voice over the
 * next block, and one more each half while it lasts. Keep each part's
 * lowest POLY note and MONO / LEGATO / UNISON lead, as the shared budget does. */
static volatile uint8_t shed_req;
static uint8_t shed_over;                              /* bit k: the half k halves ago was over 85 % */
#define SHED_TICKS ((HALF_FRAMES * 1000000u / FS) * 85u / 100u * FM1_TICKS_PER_US)   /* 85 % of a half */
static uint32_t shed_count;

/* end of a half: all = its ticks, TIMER5 nested in it included -> the render alone in us. The deadline sees
 * both; shed after 2 overloaded halves in a row (out of line: keeps the ISR's render loops as they were) */
static __attribute__((noinline)) uint32_t shed_check(uint32_t all)
{
    shed_over = (uint8_t)(shed_over << 1 | (all > SHED_TICKS));
    if ((shed_over & 3u) == 3u)
        shed_req = 1;
    return (all - t5_nested_ticks) / FM1_TICKS_PER_US;
}

static void shed_voice(void)
{
    uint32_t p, i;
    voice_t *best = 0;
    for (p = 0; p < NPART; p++)
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[p].v[i];
            if (v->active && !v->gate && v->stage != 4u && (!best || v->env < best->env))
                best = v;
        }
    if (best) {
        voice_kill(best);
        shed_count++;
        return;
    }
    for (p = 0; p < NPART; p++) {
        const track_t *t = &trk[p];
        uint32_t mode = trk_vmode(t), low = mode == V_POLY ? lowest_held(t) : 0u;
        for (i = 0; i < NVOICE; i++) {
            voice_t *v = &trk[p].v[i];
            if (v->active && v->gate && v->stage != 4u &&
                ((mode == V_POLY && i != low) || (mode != V_POLY && i > 0u))) {
                if (!best || v->age < best->age)
                    best = v;
            }
        }
    }
    if (best) {
        voice_kill(best);
        shed_count++;
    }
}

void fm1_alnk0_irq(void)                       /* via isr_alnk0 (hal/fm1_isr.S) */
{
    uint8_t p;
    uint32_t t0;
    felucca_dbg.in_audio = 1;                   /* first: TIMER5 nests from here on (main.c) */
    p = fm1_audio_pending();
    t0 = fm1_ticks();
    t5_nested_ticks = 0;
    fm1_audio_ack_aux(p);
    if (p & FM1_AUDIO_HALF) {
        uint32_t half = fm1_audio_free_half(), b, us;
        int32_t *o = &abuf[half * HALF_WORDS];
        if (shed_req) {
            shed_req = 0;
            shed_voice();
        }
        for (b = 0; b < HALF_FRAMES; b += CTL) {
#ifdef FM1_INPUT_LAT
            kb_out_tick = t0 + (HALF_FRAMES + b) * DAC_TICKS;   /* when this block plays (seq.c kb_lat) */
#endif
            audio_block(o + 2u * b, CTL);
        }
        fm1_audio_ack_half();
        cr_prof_half();                                 /* the half's stages (cr_cpu1.h): core 1's wait is in the
                                                         * half's time, so the meter and the shed see it */
        audio_halves++;
        {
            uint32_t all = fm1_ticks() - t0;
            us = shed_check(all);
            if (aw_reset) {                             /* cpu_window took the last second */
                aw_max_us = aw_sum_us = aw_n = aw_max_all = 0;
                aw_reset = 0;
            }
            if (all > aw_max_all)
                aw_max_all = all;
        }
        if (us > audio_max_us)
            audio_max_us = us;
        if (us > aw_max_us)
            aw_max_us = us;
        aw_sum_us += us;
        aw_n++;
        {
            uint32_t load = song.cpu_q8 * (CPU_AVG - 1u) + (us * 256u) / (HALF_FRAMES * 1000000u / FS) + audio_cpu_rem;
            song.cpu_q8 = load / CPU_AVG;
            audio_cpu_rem = load % CPU_AVG;
        }
        if (fm1_audio_free_half() != half)
            felucca_dbg.late++;                         /* the DMA moved on while we rendered */
        felucca_dbg.halves++;
        felucca_dbg.last_us = us;
        if (us > felucca_dbg.max_us)
            felucca_dbg.max_us = us;
        felucca_dbg.cpu_q8 = song.cpu_q8;
    }
    felucca_dbg.in_audio = 0;
}
extern void isr_alnk0(void);

static void audio_init(void)                   /* codec and ALNK0 bring-up (fm1_audio.h) */
{
    uint32_t i;
    for (i = 0; i < 2u * HALF_WORDS; i++)
        abuf[i] = 0;
    fm1_audio_init(abuf, HALF_WORDS, isr_alnk0, 3);
}
