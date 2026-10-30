/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware side of the emulator: the firmware sources (emu_firmware.h) and the hooks of emu_hooks.h.
 * Everything Felucca-specific the emulator relies on is in this file's hook bodies. */
#include <math.h>                                  /* (emu_touch_feed: a lean in octaves) */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "emu_hooks.h"

emu_hal_t emu_hal;
/* hal/fm1_input.h FM1_KEYMAP: key id at (physical column, packed row bit), -1 = none */
const int8_t emu_keymap[6][EMU_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PA0: encoders */
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},          /* PA5 */
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},          /* PA6 */
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},          /* PA7 */
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},          /* PA8 */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PB7: encoder 6 */
};

#include "emu_firmware.h"

/* ------------------------------------------------------------- flash --- */
/* emu_firmware.h builds storage.c (FELUCCA_FLASH 1) and cr_settings.c on emu_hal_fw.h's file-backed NOR; emu.c
 * parses --flash / --no-flash / --save-on-exit and hands them over here before the power-on (emu_fw_options). */
static uint8_t emu_save_on_exit;
void emu_fw_options(const char *flash_path, int no_flash, int save_on_exit, int headless)
{
    emu_save_on_exit = (uint8_t)(save_on_exit != 0);
    if (no_flash || (headless && !flash_path))
        emu_flash_path[0] = 0;                    /* RAM only: a fresh flash each run (deterministic scripts) */
    else
        snprintf(emu_flash_path, sizeof emu_flash_path, "%s", flash_path ? flash_path : "build/emu/flash.bin");
}
/* the boot guard (cr_bootguard.h, main.c fm1_cstart): what a crashed run left in .noinit, and this reset's reason */
static uint32_t emu_reset_reason = BG_P3_POWERON;
int emu_fw_boot_options(int fail, const char *reason, int stage)
{
    if (reason) {
        char *e;
        if (!strcmp(reason, "poweron") || !strcmp(reason, "power-on")) emu_reset_reason = BG_P3_POWERON;
        else if (!strcmp(reason, "wdt")) emu_reset_reason = BG_P3_WDT;
        else if (!strcmp(reason, "soft")) emu_reset_reason = BG_P3_SOFT;
        else if (!strcmp(reason, "other") || !strcmp(reason, "pin")) emu_reset_reason = 0x10u;   /* long press */
        else {
            emu_reset_reason = (uint32_t)strtoul(reason, &e, 0);
            if (*e)
                return 0;
        }
    }
    if (fail >= 0) {
        bootguard_clear(&bootguard);
        bootguard.failed = (uint32_t)fail;
        bootguard.pending = 1;                    /* the last run died within 30 s */
        felucca_dbg.magic = DBG_MAGIC;
        felucca_dbg.stage = (uint32_t)(stage >= 0 ? stage : BS_USER_SOUNDS);
    } else if (stage >= 0) {
        felucca_dbg.magic = DBG_MAGIC;
        felucca_dbg.stage = (uint32_t)stage;
    }
    return 1;
}
static void emu_boot_guard(void)                  /* main.c fm1_cstart + dbg_boot */
{
    uint32_t mode = bootguard_step(&bootguard, emu_reset_reason);
    if (mode == BOOT_UBOOT) {
        printf("boot: UBOOT (ROM boot): reset %s, the guard cleared\n", bootguard_class_name(bootguard.cls));
        fflush(stdout);
        exit(3);                                  /* (the device: fm1_enter_uboot, the PC tool's mode) */
    }
    cr_safe = mode == BOOT_SAFE;
    if (felucca_dbg.magic != DBG_MAGIC) {
        memset(&felucca_dbg, 0, sizeof felucca_dbg);
        felucca_dbg.magic = DBG_MAGIC;
    }
    felucca_dbg.boots++;
    felucca_dbg.prev_stage = felucca_dbg.stage;
    felucca_dbg.prev_rst = emu_reset_reason;
    CR_STAGE(BS_GUARD);
    printf("boot: %s reset %s failed %u pending %u counted %u prev_stage %u (%s)\n", bootguard_mode_name(mode),
           bootguard_class_name(bootguard.cls), (unsigned)bootguard.failed, (unsigned)bootguard.pending,
           (unsigned)bootguard.counted, (unsigned)felucca_dbg.prev_stage, bootguard_stage_name(felucca_dbg.prev_stage));
}

static void emu_fw_exit(void)
{
    if (!crs_loaded || cr_safe)                   /* (SAFE MODE: nothing saved) */
        return;
    if (emu_save_on_exit)
        cr_settings_save();
    else
        cr_settings_poll();
}

#define EMU_RELEASE_MS 9u            /* hal/fm1_input.h: a release needs ~9 ms open (FM1_DEB_RELEASE) */
static uint32_t emu_pa2_ms;          /* ms PA2 was high since power-on (emu_hooks.h pa2: the matrix dead; the dump) */
static uint32_t in_btn, in_key;      /* the debounced state the tick delivers */
static uint32_t btn_ms[32], key_ms[32];/* ms each went down */
/* the MASTER pot as the unit reads it (hal/fm1_adc.h: 0..1023), and main.c's own two functions on it
 * (firmware/src/master.c): the pot's level before the first block (master_boot), then smoothed a frame (master_poll) */
#if !EMU_SMK37                                    /* (the SMK-37: K1's virtual pot, hal/smk_input.h) */
#define FM1_ADC_MASTER 0u
static int32_t fm1_adc_read(uint32_t ch)
{
    int32_t a = emu_hal.master;
    (void)ch;
    return a < 0 ? 0 : a > 1023 ? 1023 : a;
}
#endif
#include "../../firmware/src/master.c"

void emu_fw_init(int demo)
{
    struct timespec ts;
    uint32_t i;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    emu_t0_ns = (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
    if (emu_hal.ready == 2u)                      /* --bench: simulated time (set before init) */
        emu_sim = 1;

    /* power-on (choralroot.c's main.c path): the flash (persist_boot), settings, panel, the engine, the sounds */
    emu_boot_guard();                             /* SAFE MODE: no flash object read or written (core.h ST_BLOCKED) */
    emu_flash_open();
    CR_STAGE(BS_FLASH);
    cr_bank_boot();                               /* persist_boot: the user sounds (upreset.c) from the flash */
    CR_STAGE(BS_SETTINGS);
    {   /* EMU_OUTPUT=classic: Options > Output held at Classic (0.18's master) whatever the settings say, so a
         * script's WAV can be compared with 0.18's bit for bit (tools/emu/test_cr.sh, docs/AUDIO-QUALITY.md) */
        const char *o = getenv("EMU_OUTPUT");
        fx_out_pin = o && !strcmp(o, "classic");
    }
    {   /* EMU_CHORUS=mono: the chorus's return as 0.18 had it, one tap to both sides (fx.c CR_CHORUS_MONO 2), so that
         * Classic can be compared with 0.18 bit for bit on scripts with a chorus send (tools/emu/classic_exact.sh) */
        const char *c = getenv("EMU_CHORUS");
        cho_mono = c && !strcmp(c, "mono");
    }
    cr_settings_boot();                           /* .. and the settings record (Felucca's fields + ChoralRoot's) */
    if (emu_hal.keys_classic && crs_last_rc == 0) {   /* the script's "keys classic" on a unit with no record: the
                                                    * defaults with Chord Keys Classic, as if chosen in the Options and
                                                    * saved (no write here: flash still holds no record) */
        crs_rec.cr.sticky = (uint8_t)(crs_rec.cr.sticky & ~CRS_LAYOUT_FM1);
        cr_settings_seal(&crs_rec.cr);
    }
    crs_ahead_boot();                             /* .. and the spare copies erased ahead (cr_shim.c persist_boot) */
    {   /* the MASTER pot at power-on: EMU_MASTER=ADC (0..1023), else where a caller put it, else 724 (-> master_q12
         * 2047, felucca_init's default); main.c reads it before the audio starts (master_boot) */
        const char *m = getenv("EMU_MASTER");
        if (m)
            emu_hal.master = atoi(m);
        else if (!emu_hal.master)
            emu_hal.master = 724;
        master_boot();
    }
#if EMU_SMK37
    emu_smk_init();                               /* main.c's fm1_input_init (SMK-37: the factory curves' defaults) */
#endif
    cr_ui_init();                                 /* the engine, the sounds, the record applied (cr_settings_load) */
    {   /* the second core (main.c, docs/DUALCORE.md): the stub runs the bass part's job when handed. EMU_C1=nostart: it
         * does not answer at power-on; EMU_C1=hang: it answers, then never finishes a job (8 ms: core 0 does it).
         * EMU_STAGES=1: the ISR's stage profile (cr_dbg; ~6.6 k host instructions a mark, left out of the stages
         * but not out of the block's count: tools/emu/perf.sh runs it apart) */
        const char *c1 = getenv("EMU_C1");
        cr_c1_test_nostart = c1 && !strcmp(c1, "nostart");
        cr_c1_test_hang = c1 && !strcmp(c1, "hang");
#if CR_C1_THREAD
        cr_c1_test_thread = c1 && !strcmp(c1, "thread");   /* the job on a second thread (cr_cpu1.h; README.md) */
#endif
        {   /* a unit that does not keep up (emu_firmware.h emu_shed_model) */
            const char *so = getenv("EMU_SHED_OVER"), *sa = getenv("EMU_SHED_AT");
            emu_shed_over = so ? (uint32_t)atoi(so) : 0u;
            if (sa && sscanf(sa, "%u:%u", &emu_shed_at0, &emu_shed_at1) != 2)
                emu_shed_at0 = emu_shed_at1 = 0;
        }
        cr_dbg.prof_on = getenv("EMU_STAGES") != NULL;
        if (cr_dbg.prof_on)
            cr_prof_calibrate();
        CR_STAGE(BS_CPU1);
        cr_c1_power_on(cr_safe, (int)bootguard.counted);   /* (main.c) */
    }
    atexit(emu_fw_exit);
    usb.up = 1;                                   /* a host is there: MIDI out flows (usb.c midi_out_event) */
    usb.config = 1;
    (void)demo;                                   /* (--demo was Felucca's 4 patterns: nothing for ChoralRoot) */
    for (i = 0; i < EMU_NB; i++)
        emu_hal.btn_id[i] = panel.btn[i];
    for (i = 0; i < EMU_NE - 1u; i++) {
        emu_hal.enc_id[i] = panel.enc[i];
        emu_hal.enc_dir[i] = panel.dir[i];
    }
    emu_hal.led_play_green = LED_PLAY_GREEN;
    lcd_fill(0, 0, 240, 240, T_BG);
    emu_hal.ready = 1;
}

#if !EMU_SMK37
/* The key sensing as the FM-1 would read it (cr_sense.c, fed here instead of by the scan's hooks; after fm1-ctl's
 * host/ctl_host.c): a key the script holds at a velocity lands at the driven reading the map gives that velocity
 * (the map inverted over its table), reads 300 pulled up (seated), and, once its rest has settled (SN_P_SETTLE and
 * a little), deeper by the lean the script gives it, so cu_touch_poll reads that pressure; a key with no velocity
 * given lands at 100; a key let go reads 1023 until the sensing has seen it open. The samples alternate driven and
 * pulled up as the key asks (dnext), as the scan does, so the velocity is known at the second pulled-up sample. */
#define EMU_TOUCH_PULL 300u
static uint16_t emu_touch_land[32];
static uint32_t emu_touch_t0[32];
static uint16_t emu_land_for(uint32_t vel)        /* the smallest driven reading the map at Velocity Curve 0 reads as vel
                                                   * (or the nearest): the script's velocity is the press's strength on
                                                   * Velo's scale, so another Curve reads the same press differently */
{
    uint32_t d, best = SN_FLOOR, bd = 255;
    uint8_t curve = cr_touch_curve;
    if (vel >= 127u)
        return (uint16_t)(SN_FLOOR - 1u);
    cr_touch_curve = 3;
    for (d = SN_FLOOR; d <= SN_CEIL; d++) {
        uint32_t v = sn_velocity(d), e = v > vel ? v - vel : vel - v;
        if (e < bd) {
            bd = e;
            best = d;
        }
        if (v <= vel)
            break;
    }
    cr_touch_curve = curve;
    return (uint16_t)best;
}
#define EMU_PROBE_BASE 770u                        /* PA10 at rest (Velo: the LED conducting on the pull-up) */
static uint32_t emu_probe_rise(uint32_t vel)      /* the probe's rise above its baseline that the curve reads as vel */
{
    uint32_t r;
    for (r = 0; r < SN_P_HI; r++)
        if (sn_probe_velocity(r) >= vel)
            return r;
    return SN_P_HI;
}
static void emu_probe_feed(uint32_t held)         /* the top row through the probe (every 3 ms: the unit's 3.3) */
{
    uint32_t k;
    for (k = SN_TOP0; k < SN_TOP0 + SN_NTOP; k++) {
        uint32_t down = (held >> k) & 1u, vel = emu_hal.key_vel[k] ? emu_hal.key_vel[k] : 100u;
        cr_sense_probe(k, down ? EMU_PROBE_BASE + emu_probe_rise(vel) : EMU_PROBE_BASE, down);
    }
}
static void emu_touch_feed(uint32_t ms, uint32_t held)
{
    uint32_t k;
    for (k = 0; k < 20u; k++) {                    /* the keys on the analog rows */
        volatile sn_key_t *s = &sn_key[k];
        if (!((held >> k) & 1u)) {
            emu_touch_t0[k] = 0;
            if (s->st != SN_OPEN) {
                if (s->dnext)
                    cr_sense_sample(k, 1023u, 1);
                cr_sense_sample(k, 1023u, 0);
            }
            continue;
        }
        if (s->st == SN_OPEN)
            emu_touch_land[k] = emu_land_for(emu_hal.key_vel[k] ? emu_hal.key_vel[k] : 100u);
        if (s->dnext) {
            uint32_t d = emu_touch_land[k];
            if (s->st == SN_DOWN && !emu_touch_t0[k])
                emu_touch_t0[k] = ms;
            if (emu_touch_t0[k] && ms - emu_touch_t0[k] > SN_P_SETTLE + 20u && emu_hal.key_lean[k]) {
                double span = (double)SN_P_SPAN[cr_touch_atsens > 2u ? 0u : cr_touch_atsens];
                double lean = ((double)SN_P_DEAD + (double)emu_hal.key_lean[k] * span / 100.0) / 256.0;   /* octaves */
                double r = (double)(d > SN_DBASE ? d - SN_DBASE : 1u) / pow(2.0, lean);
                d = SN_DBASE + (uint32_t)(r > 2.0 ? r + 0.5 : 2.0);
            }
            cr_sense_sample(k, d, 1);
        } else {
            cr_sense_sample(k, EMU_TOUCH_PULL, 0);
        }
    }
}

#endif /* !EMU_SMK37 */

/* the 1 ms timer: what fm1_timer5_irq / fm1_input_tick give the main loop */
void emu_fw_tick(uint32_t ms)
{
#if EMU_SMK37                                     /* the SMK-37: its input layer's ten scan ticks (emu_smk_hal.h) */
    fm1_irq_off();
    if (emu_sim)
        emu_sim_us = ms * 1000u;
    if ((int32_t)(ms - fm1_ms) > 0)
        fm1_ms = ms;
    emu_smk_tick(ms);
    fm1_irq_on();
    return;
#endif
#if !EMU_SMK37
    uint32_t b, k, i, down, dead;
    fm1_irq_off();
    dead = emu_hal.pa2;
    if (emu_sim)
        emu_sim_us = ms * 1000u;
    if ((int32_t)(ms - fm1_ms) > 0)
        fm1_ms = ms;
    b = emu_hal.buttons | __atomic_exchange_n(&emu_hal.buttons_tap, 0u, __ATOMIC_SEQ_CST);
    k = emu_hal.keys | __atomic_exchange_n(&emu_hal.keys_tap, 0u, __ATOMIC_SEQ_CST);
    if (dead) {                                   /* PA2 high (emu_hooks.h): the 595 chain is dead, every row reads */
        b = k = 0;                                /* open: no key, no button (a held one is let go), no knob step */
        emu_pa2_ms++;
    }
    down = b & ~in_btn;
    for (i = 0; i < EMU_NBTN; i++) {
        uint32_t m = 1u << i;
        if (down & m)
            btn_ms[i] = ms;
        else if ((in_btn & m) && !(b & m) && ms - btn_ms[i] < EMU_RELEASE_MS)
            b |= m;                               /* (held at least the debounce time) */
    }
    in_btn = b;
    if (down)
        __atomic_fetch_or(&host_pressed, down, __ATOMIC_SEQ_CST);
    down = k & ~in_key;
    for (i = 0; i < EMU_NKEY; i++) {
        uint32_t m = 1u << i;
        if (down & m) {
            key_ms[i] = ms;
            if (i < 27u)
                emu_key_t0[i] = fm1_ticks();      /* (hal/fm1_input.h note_t0: the scan that saw it closed) */
        } else if ((in_key & m) && !(k & m) && ms - key_ms[i] < EMU_RELEASE_MS)
            k |= m;
    }
    in_key = k;
    if (down)
        __atomic_fetch_or(&host_notes, down, __ATOMIC_SEQ_CST);
    fm1_in.buttons = in_btn;
    fm1_in.notes = in_key;
    if (fm1_an.probe ? ms % 3u == 0u : (ms & 1u) == 0u)   /* the analog rows' samples: every 2 ms (the unit: 2.2), every
                                                    * 3 while the top row's probe takes a third frame phase (3.3) */
        emu_touch_feed(ms, in_key);
    if (fm1_an.probe && ms % 3u == 1u)            /* the probe's readings, between them */
        emu_probe_feed(in_key);
    for (i = 0; i < EMU_NENC; i++) {
        int32_t s = __atomic_exchange_n(&emu_hal.enc[i], 0, __ATOMIC_SEQ_CST);
        if (s && !dead)
            __atomic_fetch_add(&host_enc[i], s, __ATOMIC_SEQ_CST);
    }
    fm1_irq_on();
#endif
}

static uint32_t real_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(((uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec) / 1000u);
}

void emu_fw_frame(void)                           /* main.c's loop body (choralroot.c: the same calls) */
{
    uint32_t t0, d, ft0 = fm1_ticks();             /* (audio.c ui_ft: the frame's own time, as main.c) */
    fm1_irq_off();
    t0 = real_us();
    master_poll();                                /* main.c: the MASTER pot (ADC) through its IIR */
    cr_ui_input();
    cr_ui_frame();                                /* the engine's snapshot, the LEDs */
#if EMU_SMK37
    cr_smk_frame();                               /* main.c (SMK-37): the faders, the wheels; the pads' colours */
#endif
    d = real_us() - t0;
    fm1_irq_on();
    if (d > emu_hal.ui_lock_max_us)
        emu_hal.ui_lock_max_us = d;
    cr_ui_draw();
    ui_ft_add(ft0);
    if (!usb.sx_on && usb.sx_len >= 6u && usb.sysex[usb.sx_len - 1u] == 0xF7u && usb.sysex[1] == 0x7Du) {
        crb_handle(usb.sysex + 1, usb.sx_len - 2u);   /* our SysEx (7D 46 4C), as main.c's ed_service: the handler
                                                       * (scripts' "sysex"); the reply has no transport here */
        usb.sx_len = 0;
    }
    cr_update_poll();                             /* main.c: OCT- + OCT+ + HOME held 5 s (cr_backup.c; SAFE MODE too) */
    if (!cr_safe)
        cr_settings_poll();                       /* main.c's settings_poll: a change saved once things are quiet */
    if (fm1_ms > BG_SETTLE_MS && bootguard.pending)
        bootguard_settled(&bootguard);            /* main.c: 30 s up, the boot was good */
    felucca_dbg.stage = BS_LOOP_IDLE;
}

void emu_fw_idle(void)                            /* main.c: the input scan while it waits for the next frame */
{
    fm1_irq_off();
    cr_ui_input();
    fm1_irq_on();
#if EMU_SMK37
    cr_smk_wheels();                              /* main.c (SMK-37): the bends follow the wheel, not the screen */
    cr_smk_pedal();                               /* .. and the pedal jack */
#endif
#if FELUCCA_GRAIN
    grain_poll();                                 /* main.c's idle loop: GRAIN's render, a slice a turn */
#endif
}

void emu_fw_audio(int16_t *out, uint32_t frames)  /* the ALNK0 ISR, one half buffer per EMU_BLOCK */
{
    uint32_t f, i;
    for (f = 0; f + HALF_FRAMES <= frames; f += HALF_FRAMES) {
        const int32_t *h;
        if (!emu_cpu_try()) {   /* the UI (or the timer) is inside a critical section */
            uint32_t t0 = real_us(), d;
            fm1_irq_off();
            d = real_us() - t0;
            if (d > emu_hal.audio_wait_max_us)
                emu_hal.audio_wait_max_us = d;
        }
        if (emu_stall_blocks) {                   /* a flash erase on the device: IRQs off, the buffer zeroed */
            emu_stall_blocks--;
            emu_stall_blocks_all++;
            fm1_irq_on();
            for (i = 0; i < HALF_WORDS; i++)
                out[2u * f + i] = 0;
            continue;
        }
        emu_half ^= 1u;
        fm1_alnk0_irq();
        h = &abuf[emu_half * HALF_WORDS];
        fm1_irq_on();
        for (i = 0; i < HALF_WORDS; i++) {        /* 24-bit (Q15 << OUT_SHIFT) -> Q15 = 0 dBFS */
            int32_t v = h[i] >> OUT_SHIFT;
            out[2u * f + i] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
        }
    }
}

int emu_fw_midi_in(uint32_t pkt)
{
    int ok = 0;
    fm1_irq_off();
    if (MQ - (mi_w - mi_r) >= 8u) {               /* (usb.c ep1_take: room kept for TRS MIDI) */
        midi_in_event(pkt);
        ok = 1;
    }
    fm1_irq_on();
    return ok;
}

int emu_fw_midi_out_take(uint32_t *pkt)
{
    int ok = 0;
    fm1_irq_off();
    if (mo_r != mo_w) {                           /* usb.c ep1_tx */
        *pkt = midi_out_q[mo_r % MQ];
        mo_r++;
        ok = 1;
    }
    fm1_irq_on();
    return ok;
}

void emu_fw_dump(void)
{
    uint32_t c, i;
    fm1_irq_off();
#if EMU_SMK37                                     /* (the SMK-37: its keys, buttons, button LEDs and pads) */
    printf("fm1_in: notes %07X buttons %05X keys %010llX  fm1_ms %u  frames %u\n  button leds:", (unsigned)fm1_in.notes,
           (unsigned)fm1_in.buttons, (unsigned long long)fm1_in.keys, (unsigned)fm1_ms, (unsigned)fm1_in.frames);
    for (c = 0; c < SMK_NLED; c++)
        printf("%u", (unsigned)fm1_led[c]);
    printf("\n  pads:");
    for (c = 0; c < EMU_SMK_NPAD; c++)
        printf(" %06X", (unsigned)emu_smk.pad_rgb[c]);
    printf("\n");
#else
    printf("fm1_in: notes %07X buttons %04X  fm1_ms %u  pending edges %04X / notes %07X\n",
           (unsigned)fm1_in.notes, (unsigned)fm1_in.buttons, (unsigned)fm1_ms, (unsigned)host_pressed,
           (unsigned)host_notes);
    printf("  leds:");
    for (c = 0; c < FM1_NCOL; c++)
        printf(" %02X/%02X", fm1_led[c], fm1_led_dim[c]);
    printf("  (lit/dim per column)\n");
    printf("  leds mid:");                          /* the middle level (fm1_led_dim2: Key Mode's tonic) */
    for (c = 0; c < FM1_NCOL; c++)
        printf(" %02X", fm1_led_dim2[c]);
    printf("\n  leds faint:");                      /* the faint level (fm1_led_faint: the idle chord keys) */
    for (c = 0; c < FM1_NCOL; c++)
        printf(" %02X", fm1_led_faint[c]);
    printf("\n  key leds:");                        /* per key F3..G5: 0 off, 1 faint, 2 dim, 3 mid, 4 lit */
    for (i = 0; i < 27u; i++) {
        uint32_t r, lv = 0;
        for (c = 0; c < FM1_NCOL; c++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][c] == (int8_t)(14u + i))
                    lv = (fm1_led[c] >> r) & 1u ? 4u : (fm1_led_dim2[c] >> r) & 1u ? 3u : (fm1_led_dim[c] >> r) & 1u ? 2u
                         : (fm1_led_faint[c] >> r) & 1u ? 1u : 0u;
        printf("%s%u", i == 9u ? " " : "", (unsigned)lv);
    }
    printf("  (F3..C4 D4..G5: 0 off 1 faint 2 dim 3 mid 4 lit)\n");
#endif
    for (i = 0; i < 2u; i++) {
        uint32_t v, n = 0;
        for (v = 0; v < NVOICE; v++)
            n += trk[i].v[v].active;
        printf("  part %u: %s / %s, voices %u, level %d, voice mode %d\n", (unsigned)i, ENGINES[trk[i].engine]->name,
               cu_preset_name(trk[i].eng_req, trk[i].preset), (unsigned)n, (int)trk[i].p[P_LEVEL], (int)trk[i].p[P_VOICE]);
    }
    printf("  cr: voices %d pending %d notes-out %u chord %s%s%s sounding %u notes",
           cr_voices(&cr), cr_pending(&cr), (unsigned)cr_out_notes, cr_snap.ci.root, cr_snap.ci.qual, cr_snap.ci.sup,
           (unsigned)cr_snap.ci.sounding);
    for (i = 0; i < cr_snap.ci.nnotes; i++)
        printf(" %u", (unsigned)cr_snap.ci.notes[i]);
    printf("\n  cr: key %u tonic %u scale %u perform %u mode %u bass %u (sound %u) voicing %d octave %d bpm %u "
           "view %u layer %u options %u kkeys %u\n",
           (unsigned)cs.key_on, (unsigned)cs.tonic, (unsigned)cs.scale, (unsigned)cr.perform_on,
           (unsigned)cr.perform_mode, (unsigned)cr.bass_on, (unsigned)cs.bass_sound, (int)cr.voicing, (int)cu.octave,
           (unsigned)cs.bpm, (unsigned)cs.view, (unsigned)cu_layer(), (unsigned)cu.opt_open, (unsigned)cs.key_play);
    printf("  kbd: plays %s keyboard %s single %u split-note %u page %u (engine: notes %u full %u single %u snap %u)\n",
           cs.key_notes ? "notes" : "chords", cs.kbd_full ? "full" : "chords", (unsigned)cs.single,
           (unsigned)cr.split_note, (unsigned)cu.kpage + 1u, (unsigned)cr.key_notes, (unsigned)cr.full,
           (unsigned)cr.single, (unsigned)cr.snap);
    {                                               /* the KEY layer's Numerals and what the Chord view shows for it now */
        char rn[CR_NUMERAL_MAX];
        cu_numeral(rn);
        printf("  numerals: %s shown \"%s\"\n", CU_NUMS_TR[cs.numerals % 3u], rn);
    }
    printf("  lock: on %u latched %02X type-key %s ext-keys %s\n", (unsigned)cu.clock, (unsigned)cu.mlatch,
           cs.lock_keep ? "keeps" : "clears", cs.lock_push ? "pushed" : "toggle");   /* (latched: bit cr_mod_t) */
    {
#if EMU_SMK37
        unsigned lfo = 0;                            /* (the SMK-37: no FM-1 layout, no levels of this kind) */
#else
        uint8_t q = cu_led_pos[panel.btn[BT_LATCH] % CU_LED_IDS];   /* the LFO button's light: 4 lit 3 mid 2 dim 1 faint */
        unsigned lfo = q == 0xFFu ? 0u : ((fm1_led[q >> 3] >> (q & 7u)) & 1u) ? 4u : ((fm1_led_dim2[q >> 3] >> (q & 7u)) & 1u) ? 3u
                       : ((fm1_led_dim[q >> 3] >> (q & 7u)) & 1u) ? 2u : ((fm1_led_faint[q >> 3] >> (q & 7u)) & 1u) ? 1u : 0u;
#endif
        printf("  chord keys: layout %s types-held %02X posted %02X gesture %u kwait %07X kmidi %07X full-chk %u full-all %u"
               " suswin %u midi-keys %u full-short %u latch %u lfo-led %u\n", cu_fm1() ? "fm1" : "classic", (unsigned)cu.tk_held,
               (unsigned)cu.tposted, (unsigned)cu.g_open, (unsigned)cu.kwait, (unsigned)cu.kmidi, (unsigned)cu.full_chk,
               (unsigned)cu.full_all, (unsigned)cs.suswin, (unsigned)cs.midi_keys, (unsigned)cs.full_short,
               (unsigned)cs.sticky, lfo);
    }   /* (FM-1's
                                                      * type keys: key bits; posted: bit cr_mod_t; docs/CHORD-LAYOUT.md) */
    {                                               /* the engine's side of the chord keys: down (held or latched),
                                                     * what the chord keys' LEDs show, the voices' own specs */
        unsigned v, fr = 0;
        printf("  mods: down %02X active %02X voices", (unsigned)cr.mods_down, (unsigned)cr_mods_active(&cr));
        for (v = 0; v < CR_MAX_VOICES; v++)
            if (cr.v[v].used) {
                printf(" %u:q%u/x%X%s", (unsigned)cr.v[v].key, (unsigned)cr.v[v].sq, (unsigned)cr.v[v].sx, cr.v[v].is_free ? "f" : "");
                fr += cr.v[v].is_free;
            }
        printf(" free %u\n", fr);
    }
    printf("  parts busy %u  octave %d master_q12 %u cpu %u%%  midi in %u out %u\n", (unsigned)cr_parts_busy(),
           (int)cu.octave, (unsigned)song.master_q12, (unsigned)(song.cpu_q8 * 100u / 256u),
           (unsigned)(mi_w - mi_r), (unsigned)(mo_w - mo_r));
    {
        unsigned hid = 0, k;
        for (k = 0; k < crl.d.nev; k++)
            hid += (crl.d.ev[k].layer & CRL_HID) != 0;
        printf("  loop: state %u cap %u layers %u events %u len %u slot %u played %u ring %u used %03X metro %u sig %u "
               "mode %u keys %u step %u hidden %u busy-loopv %d style %u single %u split %u\n",
               (unsigned)crl.state, (unsigned)crl.cap, (unsigned)crl.d.nlayers, (unsigned)crl.d.nev, (unsigned)crl.d.len,
               (unsigned)cs.loop_slot + 1u, (unsigned)crl.played, (unsigned)cr_loop_ring(&crl), (unsigned)cs.loop_used,
               (unsigned)crl.metro, (unsigned)crl.sig, (unsigned)crl.mode, (unsigned)cs.loop_keys, (unsigned)crl.step, hid,
               cr_loop_busy(&cr, 0) + cr_loop_busy(&cr, 1) + cr_loop_busy(&cr, 2),
               (unsigned)cr.playstyle, (unsigned)cr.single, (unsigned)cr.split_pc);
        /* 0.18: the Rate (in force / asked for), the changes not saved yet, the PLAY menu's page, the loop editor (on,
         * the cursor's first tick, its notes' length in steps, the grid, the extent with the hidden steps), a global
         * save waiting; step entry: the steps used, the set Length's ticks (0: Free), chords refused (a 65th) */
        printf("  loop2: rate %u want %u dirty %u page %u edit %u t0 %u long %u grid %u ext %u save-pending %u "
               "steps-used %u step-len %u refused %u\n",
               (unsigned)crl.rate, (unsigned)crl.rate_want, (unsigned)crl.dirty, (unsigned)cu.lpage + 1u, (unsigned)crl.ed,
               (unsigned)crl.ed_t0, (unsigned)(crl.ed ? cr_loop_ed_nlen(&crl) : 0u), (unsigned)crl.ed_g, (unsigned)crl.ed_len,
               (unsigned)cu.gsave_pend, (unsigned)crl.step_end, (unsigned)crl.step_len, (unsigned)crl.step_no);
        /* 0.18: the loop editor's page and its cursor step's properties (-1: none), its notes' part of a step
         * (quarters; 0: whole steps); the pass of the loop (mod 12); the steps that have a property; the loop voices
         * in use (sounding or a ratchet between two hits) */
        {
            unsigned q, np = 0, nv = 0, npre = 0;
            for (q = 0; q < CRL_STEPS; q++)
                np += crl.d.st[q][0] || crl.d.st[q][1];
            for (q = 0; q < (unsigned)CR_MAX_LOOPV; q++)
                nv += crl.pv[q].used != 0;
            for (q = 0; q < (unsigned)CR_MAX_LOOPV; q++)   /* the step preview's voices (0.22, issue 8) */
                npre += (crl.pre >> q) & 1u;
            printf("  loop3: epage %u cursor %u ratchet %d cond %d voicing %d chance %d frac %u pass %u props %u loopv %u pre %u\n",
                   (unsigned)cu.le_page + 1u, (unsigned)(crl.ed_t0 / CRL_STEP) + 1u,
                   crl.ed ? cr_loop_ed_prop(&crl, CRL_SP_RATCH) : -1, crl.ed ? cr_loop_ed_prop(&crl, CRL_SP_COND) : -1,
                   crl.ed ? cr_loop_ed_prop(&crl, CRL_SP_VOIC) : -1, crl.ed ? cr_loop_ed_prop(&crl, CRL_SP_CHANCE) : -1,
                   (unsigned)(crl.ed ? cr_loop_ed_nfrac(&crl) : 0u), (unsigned)crl.pass12, np, nv, npre);
        }
        /* 0.18: the loop editor's cursor step: its events as root/qx (in the array's order: the last one names it);
         * the chord the loop struck last since it started (the editor's playing name; -: none) */
        {
            unsigned q, n = 0;
            printf("  loop4: step-events");
            for (q = 0; crl.ed && q < crl.d.nev; q++)
                if (!(crl.d.ev[q].layer & CRL_HID) && crl.d.ev[q].t >= crl.ed_t0 && crl.d.ev[q].t < crl.ed_t0 + CRL_STEP) {
                    printf(" %u/%02X", (unsigned)crl.d.ev[q].root, (unsigned)crl.d.ev[q].qx);
                    n++;
                }
            printf(" count %u", n);
            if (crl.hit_on)
                printf(" hit %u/%02X\n", (unsigned)crl.hit_root, (unsigned)crl.hit_qx);
            else
                printf(" hit -\n");
        }
    }
    {   /* the loop's parts (0.18): its voices sounding by part (a recorded Split Bass bass-side note: the bass part),
         * its live events of the bass side, the notes on the engine's main / bass streams (looped and played) */
        unsigned k, lc = 0, lb = 0, be = 0, sm = 0, sb = 0;
        for (k = 0; k < (unsigned)CR_MAX_LOOPV; k++) {
            const cr_voice_t *v = &cr.v[CR_MAX_VOICES + CR_MAX_PADS + k];
            if (v->used && (v->zone & CR_Z_BASS))
                lb++;
            else if (v->used)
                lc++;
        }
        for (k = 0; k < crl.d.nev; k++)
            be += !(crl.d.ev[k].layer & CRL_HID) && CRL_EV_BASS(&crl.d.ev[k]);
        for (k = 0; k < 128u; k++) {
            sm += cr_sounding(&cr, CR_STREAM_MAIN, (uint8_t)k) != 0;
            sb += cr_sounding(&cr, CR_STREAM_BASS, (uint8_t)k) != 0;
        }
        printf("  loopv: chord %u bass %u bass-side-events %u stream-notes main %u bass %u\n", lc, lb, be, sm, sb);
    }
    {   /* the version, everywhere it shows: Options > Version (opt_text), the identity over SysEx (cr_backup.c's
         * INFO reply, built by the handler itself: no transport here, CRB_SEND is empty), the splash's foot line */
        static const uint8_t q[4] = {CRB_HDR0, CRB_HDR1, CRB_HDR2, CRB_INFO};
        char ov[40], id[40];
        uint32_t k = 0;
        opt_text(O_VERSION, ov, sizeof ov);
        id[0] = 0;
        if (crb_handle(q, 4))
            for (; k < sizeof id - 1u && CRB_OUT[5u + k] && CRB_OUT[5u + k] != 0xF7u; k++)
                id[k] = (char)CRB_OUT[5u + k];
        id[k] = 0;
        printf("  version: options '%s' identity '%s' build '%s'\n", ov, id, FELUCCA_VERSION);
        {                                            /* the Options: the entry selected (its place among those
                                                      * listed, cu_opt_vis: what SELECT counts) and its value's text */
            uint32_t o, at = 0, n = 0;
            for (o = 0; o < cu_opt_n(); o++)
                if (cu_opt_vis(o)) {
                    if (o == cu.opt_sel)
                        at = n;
                    n++;
                }
            opt_text(cu.opt_sel, ov, sizeof ov);
            printf("  options: open %u entry %u of %u '%s' value '%s'\n", (unsigned)cu.opt_open, (unsigned)at,
                   (unsigned)n, O_NAME[cu.opt_sel % O_N], ov);
        }
    }
    printf("  boot: %s reset %s failed %u pending %u prev_stage %u\n", bootguard_mode_name(bootguard.mode),
           bootguard_class_name(bootguard.cls), (unsigned)bootguard.failed, (unsigned)bootguard.pending,
           (unsigned)felucca_dbg.prev_stage);
    printf("  lcd: screen %s, %u writes\n", cu.screen_off ? "off (a black frame, nothing drawn)" : "on",
           (unsigned)emu_hal.lcd_writes);        /* (cr_ui.c's screen off: the panel itself stays on and lit) */
    printf("  pa2: %s, high for %u ms since power-on (hal/fm1_lcd_hw.h)\n",   /* (emu_hooks.h pa2) */
           emu_hal.pa2 ? "HIGH: the backlight off, no key, button or knob read, no LED lit"
                       : "low: the backlight, the key matrix and the LEDs alive", (unsigned)emu_pa2_ms);
    printf("  flash: %s, %u writes, settings saves %u (record: %s)\n", emu_flash_path[0] ? emu_flash_path : "RAM only",
           (unsigned)emu_flash_writes, (unsigned)crs_saves, crs_last_rc == 1 ? "current" : crs_last_rc == 2 ? "migrated" : "defaults");
    {   /* the favourites (cr_favs.c): the marks in all, the flash writes, one still to be written, what power-on found,
         * ", filter on" while the favourites filter is, then every mark as tag:id (tag: the engine's index, 128 the user
         * slots; P5's id: the Sequential number), "c" / "b" after a mark typed Chord / Bass */
        unsigned k, i, off = 0;
        printf("  favs: %u marked, saves %u, pending %u (at power-on: %s)%s :", (unsigned)crf_count(), (unsigned)crf_saves,
               (unsigned)crf_dirty, crf_last_rc == 1 ? "read" : crf_last_rc < 0 ? "not understood" : "none",
               crf_filter ? ", filter on" : "");
        for (k = 0; k < CRF_NSEC; off += CRF_SEC[k].bytes, k++)
            for (i = 0; i < CRF_SEC[k].bytes * 8u; i++)
                if ((crf_bits[off + i / 8u] >> (i % 8u)) & 1u)
                    printf(" %u:%u%s", (unsigned)CRF_SEC[k].tag, i,
                           crf_type(CRF_SEC[k].tag, i) == CRF_CHORD ? "c" : crf_type(CRF_SEC[k].tag, i) == CRF_BASS ? "b" : "");
        for (k = 0; k < CRF_NSEC; k++)             /* (.. then "~tag:id" + "c" / "b": a sound typed and not marked) */
            for (i = 0; i < CRF_SEC[k].bytes * 8u; i++)
                if (!crf_has(CRF_SEC[k].tag, i) && crf_type(CRF_SEC[k].tag, i) != CRF_BOTH)
                    printf(" ~%u:%u%s", (unsigned)CRF_SEC[k].tag, i, crf_type(CRF_SEC[k].tag, i) == CRF_CHORD ? "c" : "b");
        printf("\n");
    }
    {   /* the session (cr_settings.c css_*, docs/SETTINGS.md): its saves, what power-on found, the state as it would be
         * saved now and as flash holds it (cr_session_hash; "pending 1": they differ, a global save would write it),
         * and the state itself: the FX layer, each part's sound */
        static cr_session_t x;
        unsigned k;
        fm1_irq_on();                             /* (css_capture takes the lock itself) */
        css_capture(&x);
        fm1_irq_off();
        printf("  session: saves %u (at power-on: %s) now %08X flash %08X pending %u  voicing %d octave %d bass %u lock %u "
               "latched %02X\n", (unsigned)css_saves, css_last_rc == 1 ? "current" : css_last_rc == 2 ? "migrated" :
               css_last_rc < 0 ? "not these settings'" : "none", (unsigned)cr_session_hash(&x), (unsigned)css_flash,
               (unsigned)(cr_session_hash(&x) != css_flash), (int)x.voicing, (int)x.octave, (unsigned)x.bass_on, (unsigned)x.lock, (unsigned)x.latched);
        printf("  session fx: on %u sel %u (%s %u/%u) amt", (unsigned)cs.fx_on, (unsigned)cs.fx_sel, cu_fx_pgname(),
               cs.fx_page >= CU_FXPG_SENDS ? 1u : (unsigned)cs.fx_page + 1u, (unsigned)CU_FXP[cu_fx_pg()].of);
        for (k = 0; k < CU_NFX; k++)
            printf(" %u", (unsigned)cs.fx_amt[k]);
        printf(" sends");
        for (k = 0; k < CU_NFX; k++)
            printf(" %d", (int)trk[CR_PART_CHORD].p[CU_FX[k].send]);
        printf(" par");
        for (k = 0; k < x.fx.n; k++)
            printf(" %u=%d", (unsigned)x.fx.par[k].tag, (int)x.fx.par[k].v);
        printf("\n");
        for (k = 0; k < 2u; k++) {                /* the sound editor's FX group as its cells read it (cr_edit.c ce_param:
                                                   * what the screen shows), beside the track's own (what the audio reads) */
            unsigned c;
            printf("  editor fx %s:", k ? "bass" : "chord");
            for (c = 0; c < 4u; c++) {
                const track_t *t = &trk[k ? CR_PART_BASS : CR_PART_CHORD];
                int32_t v = 0;
                ce_ref_t r = ce_r(CE_R_TRK, CP_PAGES[CP_FX].id[c], 0);
                const param_desc_t *d = ce_param(t, r, &v);
                char b[16];
                ce_text(t, r, d, v, b, sizeof b);  /* (the cell's own text: cr_ui.c cu_send_text) */
                printf(" %s %d %s", d->label, (int)v, b);
            }
            printf(" track");
            for (c = 0; c < 4u; c++)
                printf(" %d", (int)trk[k ? CR_PART_BASS : CR_PART_CHORD].p[CP_PAGES[CP_FX].id[c]]);
            printf("\n");
        }
        /* the drive of each part (docs/FX.md "FM TONE's Drive"): what the audio reads (the track's), the part's own
         * drive send (the other engines'; it waits while the engine is FM TONE, whose Drive is the sound's) */
        printf("  drive: chord%s track %d send %d edited %u | bass%s track %d send %d edited %u\n",
               cu_quad_part(0) ? " FM TONE" : "", (int)trk[CR_PART_CHORD].p[P_DIST], (int)cs.fx_amt[3],
               (unsigned)psnd[0].edited, cu_quad_part(1) ? " FM TONE" : "", (int)trk[CR_PART_BASS].p[P_DIST],
               (int)*cu_bdrive_p(), (unsigned)psnd[1].edited);
        /* each part's trim (track_t.trim, 0.5 dB steps: the sound's, cr_bank.c CB_TRIM) beside its pool entry (kind
         * 0 INIT, 1 factory, 2 user; the factory preset; the slot): test_persist.sh, a session and a removed row */
        printf("  trim: chord %d kind %u fk %u slot %u | bass %d kind %u fk %u slot %u\n",
               (int)trk[CR_PART_CHORD].trim, (unsigned)psnd[0].kind, (unsigned)psnd[0].fk, (unsigned)psnd[0].slot,
               (int)trk[CR_PART_BASS].trim, (unsigned)psnd[1].kind, (unsigned)psnd[1].fk, (unsigned)psnd[1].slot);
        {   /* what the last frame's screen READS (docs/FX.md "One read-out": a send reads the same wherever it is
             * shown): a popup's value, its unit line and its label, and the cells that are on, row by row */
            const cr_screen_t *sc = &cu_scr;
            unsigned r, c;
            printf("  screen reads: value '%s' sub '%s' label '%s' cells", sc->value, sc->sub, sc->label);
            for (r = 0; r < CR_ED_ROWS; r++)
                for (c = 0; c < 4u; c++)
                    if (sc->cell[r][c].flags & CR_CF_ON)
                        printf(" %u.%u:%s=%s", r, c, sc->cell[r][c].label, sc->cell[r][c].value);
            printf("\n  screen foot: '%s'\n", sc->footer);   /* (the bottom line: a layer's keys, the Options') */
            if (sc->kind == CR_K_ROUTE) {          /* the FX layer's Routing page: the blocks in order, the mode, the
                                                    * block held (cr_screen.h rt_*) */
                static const char *const RB[4] = {"Chorus", "Delay", "Reverb", "Filter"};
                printf("  screen route: %s %s %s %s %s held %s fx %u filter %u\n", sc->rt_mode ? "chain" : "parallel",
                       RB[sc->rt_order[0] & 3u], RB[sc->rt_order[1] & 3u], RB[sc->rt_order[2] & 3u],
                       RB[sc->rt_order[3] & 3u], sc->rt_held ? RB[(sc->rt_held - 1u) & 3u] : "none",
                       (unsigned)sc->rt_fx_on, (unsigned)sc->rt_filter_on);
            }
        }
        for (k = 0; k < 2u; k++)
            printf("  session %s: %s / %s edited %u kind %u crc %04X level %d pan %d\n", k ? "bass" : "chord",
                   ENGINES[trk[k].eng_req % NENGINES]->name, psnd[k].name, (unsigned)psnd[k].edited, (unsigned)psnd[k].kind,
                   (unsigned)cu_snd_crc(&trk[k]), (int)trk[k].p[P_LEVEL], (int)trk[k].p[P_PAN]);
    }
    printf("  voices: given up %u (budget fades + overload sheds %u), own voices taken for a new note %u\n",
           (unsigned)voice_kills, (unsigned)shed_count, (unsigned)voice_steals);
    if (emu_shed_over || emu_shed_at1)            /* (the overload model alone: no line in any other run) */
        printf("  shed: halves over 85 %% %u, last voice %05X why %u at %u us (audio.c shed_check; the model: "
               "EMU_SHED_OVER %u, EMU_SHED_AT %u:%u)\n", (unsigned)cr_dbg.shed_over_n, (unsigned)cr_dbg.shed_last,
               (unsigned)cr_dbg.shed_why, (unsigned)cr_dbg.shed_us, (unsigned)emu_shed_over, (unsigned)emu_shed_at0, (unsigned)emu_shed_at1);
    printf("  flash erases with the audio stalled %u (%u blocks of silence, %u ms each erase)\n", (unsigned)emu_stalls,
           (unsigned)emu_stall_blocks_all, (unsigned)EMU_ERASE_MS);
    /* the quiet writes (cr_settings.c, storage.c "erase ahead"): every erase (at power-on too: no audio to stall),
     * those done ahead, those the unit started by itself near playing (the rule: 0), its own saves, the interrupts'
     * masked time by the model (EMU_ERASE_MS an erase, EMU_PROG_US a page), each spare copy's state, and when the
     * last erase started against the last input (ms since power-on) */
    printf("  quiet writes: erases %u (ahead %u, at power-on %u), near %u, auto saves %u, masked %u ms, ready %u%u%u, "
           "last erase at %u ms (input at %u, now %u)\n", (unsigned)st_erases, (unsigned)crq.ahead,
           (unsigned)crq.ahead_boot, (unsigned)st_near, (unsigned)crq.auto_saves, (unsigned)(emu_masked_us / 1000u),
           (unsigned)st_ready(OBJ_SETTINGS), (unsigned)st_ready(OBJ_SESSION), (unsigned)st_ready(OBJ_FAVS),
           (unsigned)emu_erase_ms, (unsigned)cu.last_input, (unsigned)fm1_ms);
    fm1_irq_on();
    {   /* the settings as they would be saved now (crs_capture: its own IRQ lock) and as they are in the record last
         * written; the panel calibration table and HOLD (Felucca's fields of the same flash object) */
        cr_settings_t t;
        uint32_t h = 2166136261u, k;
        crs_capture(&t);
        for (k = 0; k < sizeof panel; k++) {
            h ^= ((const uint8_t *)&panel)[k];
            h *= 16777619u;
        }
        printf("  master: output %s (chain %u, want %u, fade %u) lowcut %u/%u found %u\n", cs.out_clean ? "Clean" : "Classic",
               (unsigned)fx_out_mode, (unsigned)fx_out_want, (unsigned)fx_out_fade, (unsigned)settings.lowcut,
               (unsigned)fx_lowcut, (unsigned)crs_lowcut_found);
        printf("  usb-rate: 48k %u (Options > USB Rate; the record's pick_roots %u)\n", (unsigned)cs.usb48,
               (unsigned)t.pick_roots);
        printf("  chord-names: standard %u (Options > Chord Names; the engine's %d; the record's bit %u)\n",
               (unsigned)cs.names_std, cr_chord_names(), (unsigned)CRS_NAMES(&t));
        printf("  sixth: up %u (Options > 6th with 7th; the engine's %u; the record's bit %u)\n",
               (unsigned)cs.sixth_up, (unsigned)cr.sixth_up, (unsigned)CRS_SIXTH(&t));
        printf("  keys-record: fm1 %u moved %u saved-fm1 %u saved-moved %u (Options > Chord Keys in the record: bit 1 of "
               "sticky; 0.25.1's move to FM-1: bit 1 of leds)\n", CRS_LAYOUT(&t), (unsigned)((t.leds & CRS_KEYS_MOVED) != 0u),
               CRS_LAYOUT(&crs_rec.cr), (unsigned)((crs_rec.cr.leds & CRS_KEYS_MOVED) != 0u));
        printf("  clean: look-ahead %u samples, the limiter's gain %d of 32768\n", (unsigned)CLN_LA, (int)cln_g);
        printf("  settings: now %08X saved %08X style %u extadd %u key %u motion %u leds %u hold %u dualcore %u usb %u/%u "
               "view %u chord-sound %04X slot %u pending %u  panel %08X\n",
               (unsigned)t.check, (unsigned)crs_rec.cr.check, (unsigned)t.playstyle, (unsigned)t.extadd, (unsigned)t.key_on,
               (unsigned)t.motion, CRS_LEDS(&t), (unsigned)settings_hold, (unsigned)CRS_DUALCORE(&t),
               (unsigned)t.usb_in, (unsigned)t.usb_level, (unsigned)t.view, (unsigned)t.chord_sound, (unsigned)t.loop_slot,
               (unsigned)crs_pending, h);
    }
}

/* the screen the last UI frame drew (emu.c EMU_UI_LOG): its kind, view, the texts that pick its glyphs, the animation
 * clock, the strips drawn and blitted */
uint32_t emu_fw_layout(void) { return (uint32_t)cu_fm1(); }
void emu_fw_ui_info(char *buf, uint32_t n)
{
    static const char *const K[] = {"none", "stripes", "chord", "picker", "meter", "keyboard", "arp", "params", "geek",
                                    "text", "big", "scope", "edit8", "stack", "knobrow", "loopmenu"};
    static const char *const DM[] = {"play", "armed", "rec", "od"};    /* cr_screen.h CR_DIAL_* */
    const cr_screen_t *s = &cu_scr;
    snprintf(buf, n, "%s view %u name '%s|%s|%s' from '%s|%s|%s' item '%s' value '%s' title '%s' size %u squeeze %u "
             "anim %02X %u ms ring %u/%u dial %s/%u/%u msg '%s' blits %u/%u bytes %u wv %u,%u,%u,%u,%u "
             "lm %u/%u/%u/%u lanes %u/%u slot %u jump %u sub '%s' right '%s' foot '%s' now '%s' hot %u.%u",
             s->kind < CR_K_N && s->kind < sizeof K / sizeof K[0] ? K[s->kind] : "?", (unsigned)cs.view, s->name.root, s->name.quality, s->name.sup,
             s->from.root, s->from.quality, s->from.sup, s->kind == CR_K_PICKER || s->kind == CR_K_KNOBROW || s->kind == CR_K_LOOPMENU ? cr_item(s, s->sel) : "",
             s->value, s->title, (unsigned)s->size, (unsigned)s->squeeze, (unsigned)s->anim,
             (unsigned)cr_anim_ms(&cu_anim, cu_now()), (unsigned)s->ring_on, (unsigned)s->ring,
             !s->dial_on ? "-" : s->dial_mode < 4u ? DM[s->dial_mode] : "?", (unsigned)s->dial, (unsigned)s->dial_dot, s->message,
             (unsigned)cr_dc.blits, (unsigned)cr_dc.drawn, (unsigned)cr_dc.bytes, (unsigned)s->wv[0], (unsigned)s->wv[1], (unsigned)s->wv[2], (unsigned)s->wv[3],
             (unsigned)s->wv[4], (unsigned)s->lm_mid, (unsigned)s->lm_bars,
             (unsigned)s->lm_free, (unsigned)(s->lm_pos & 0x7FFFu), (unsigned)s->lm_nlane, (unsigned)s->lm_newlane,
             (unsigned)s->lm_slot + 1u, (unsigned)s->lm_jump, s->sub, s->right, s->kind == CR_K_LOOPMENU ? s->foot : "", s->kind == CR_K_LOOPMENU ? s->lm_now : "",
             (unsigned)s->hot_r, (unsigned)s->hot_c);
    if (s->kind == CR_K_METER) {                  /* a meter's stripes in this frame: lit of all, the fill's start and end,
                                                   * its label (tools/emu/test_cr.sh: the sound read-out's steps) */
        size_t l = strlen(buf);
        cr_frame_t fr;
        cr_frame(s, cr_anim_ms(&cu_anim, cu_now()), &fr);
        snprintf(buf + l, n - l, " fill %u of %u from %u to %u label '%s' over '%s'", (unsigned)fr.filled,
                 (unsigned)cr_segs(s), (unsigned)cr_filled(s, s->pct_from), (unsigned)cr_filled(s, s->pct), s->label,
                 s->line);                        /* (over: the line over the value, a pool's read-out's engine) */
    }
    if (s->kind == CR_K_ARP) {                    /* the arp screen's hop and its rate line (tools/emu/test_cr.sh) */
        size_t l = strlen(buf);
        snprintf(buf + l, n - l, " pos %d of %u line '%s'", (int)s->pos, (unsigned)s->n_notes, s->line);
    }
    if (s->kind == CR_K_KNOBROW && s->kr_band == 2u) {   /* the PERF layer's arp band (the picker's lines end at "hot") */
        size_t l = strlen(buf);
        snprintf(buf + l, n - l, " band arp pos %d of %u label '%s'", (int)s->pos, (unsigned)s->n_notes, s->label);
    }
    if (s->kind == CR_K_GEEK) {                   /* GEEK OUT's status lines (the second core's: tools/emu/test_cr.sh) */
        size_t l = strlen(buf);
        uint32_t i;
        for (i = 0; i < s->n_lines && l + 4u < n; i++) {
            snprintf(buf + l, n - l, " line%u '%s'", (unsigned)i, s->lines[i].t);
            l = strlen(buf);
        }
    }
}
uint32_t emu_fw_keys(uint32_t v[5])
{
    v[0] = cr_klat.n;
    v[1] = CR_KLAT_US(cr_klat.pick);
    v[2] = CR_KLAT_AVG_US();
    v[3] = CR_KLAT_US(cr_klat.eng);
    v[4] = cu_pumped;
    return cr_dc.drawn;
}
void emu_fw_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = shed_count;
    *cpu_pct = song.cpu_q8 * 100u / 256u;
}

/* the audio ISR's stages since power-on (cr_cpu1.h cr_dbg; EMU_STAGES=1), in device us (host instructions / 259) */
void emu_fw_stages(char *buf, uint32_t n)
{
    uint32_t k, o = 0;
    buf[0] = 0;
    if (!cr_dbg.prof_on || !cr_dbg.tot_n)
        return;
    o += (uint32_t)snprintf(buf + o, n - o, "stages (device us per half, avg / max): ");
    for (k = 0; k < CRP_N && o < n; k++)
        o += (uint32_t)snprintf(buf + o, n - o, "%s%s %.0f / %.0f", k ? ", " : "", CRP_NAME[k],
                                (double)cr_dbg.tot_sum[k] / cr_dbg.tot_n / CR_PROF_PER_US,
                                (double)cr_dbg.tot_max[k] / CR_PROF_PER_US);
    if (o < n)
        o += (uint32_t)snprintf(buf + o, n - o, "; c1 %s, jobs %u, reverb jobs %u", cr_c1_name(), (unsigned)cr_dbg.c1_jobs,
                                (unsigned)cr_dbg.c1x_jobs);   /* (3y50: the reverb on core 1, fx.c mix_fx_split) */
    /* the fx stage's parts, over the halves in which the effects ran (the unit's 73 DEBUG has the same four) */
    if (o < n && cr_dbg.x_tot_busy) {
        o += (uint32_t)snprintf(buf + o, n - o, "\ncpu: fx parts (device us per half, avg / max over %u halves with the "
                                "effects running): ", (unsigned)cr_dbg.x_tot_busy);
        for (k = 0; k < CRX_N && o < n; k++)
            o += (uint32_t)snprintf(buf + o, n - o, "%s%s %.0f / %.0f", k ? ", " : "", CRX_NAME[k],
                                    (double)cr_dbg.x_tot_sum[k] / cr_dbg.x_tot_busy / CR_PROF_PER_US,
                                    (double)cr_dbg.x_tot_max[k] / CR_PROF_PER_US);
    }
}
