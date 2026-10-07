/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* ChoralRoot FM-1 (Felucca's) boot and main loop: the splash says CHORALROOT and the version; OCT- + OCT+ held at
 * power-on still runs the panel calibration (panel_setup) and saves it (settings_save). Boot order: WDT first, boot-loop guard, fatal vectors,
 * guards; then LCD, input (TIMER5 IRQ, 10 kHz), audio (ALNK0 IRQ).
 * The boot guard (cr_bootguard.h): a power-on clears it; a watchdog or soft (crash) reset within 30 s of the boot
 * before counts; 2 in a row -> SAFE MODE (cr_safe: no flash object loaded or saved, factory sounds, no USB audio
 * stream; the installer and OCT- + OCT+ 5 s work), 4 -> UBOOT. Every boot step leaves its number in felucca_dbg.stage
 * (.noinit): after a crash prev_stage says where (console `boot`, the SAFE MODE screen, GEEK OUT). */
extern uint32_t _data_start[], _data_end[], _data_load[], _bss_start[], _bss_end[];
extern uint32_t _pool_start[], _pool_end[], _rt_start[], _rt_end[], _rt_load[];


/* TIMER5 outranks ALNK0, so the scan keeps its 100 us pace while a half buffer renders: before,
 * the ticks stopped for the whole render (0.7 ms idle, several ms loaded), the column lit when it
 * began stayed lit that long (a ~16 Hz flicker over all LEDs, beating with the scan) and the
 * encoders lost frames. Nested in ALNK0 it only scans (GPIO + fm1_in, nothing the audio ISR touches)
 * and counts ms; USB and UART polls wait for the first tick after the render, as they always did,
 * and the time spent nested is handed to the audio ISR so its load figures stay render-only.
 * The USB audio streams cannot wait for the render (a packet each way per 1 ms frame, a render takes up to
 * ~5 ms): ua_service also runs nested, by elapsed time (every 250 us at most: work spanning ticks does not
 * stretch the next deadline). It touches only the audio endpoints (INDEX is set on every access) and the
 * USB side of the audio rings (audio.c copies with the IRQs off), and usb_poll never runs nested, so the
 * two never interleave (Melodee's). */
void fm1_timer5_irq(void)
{
    static uint32_t sub, owed;
    uint32_t t0 = fm1_ticks(), usb_due = sub % 5u == 0u;
    fm1_timer5_ack();
    felucca_dbg.timer_irqs++;
    fm1_input_tick();
    {   /* milliseconds from the 24 MHz TIMER4 (robust to a late tick) */
        static uint32_t last, acc;
        acc += t0 - last;
        last = t0;
        while (acc >= 1000u * FM1_TICKS_PER_US) {
            acc -= 1000u * FM1_TICKS_PER_US;
            fm1_ms++;
        }
    }
    if (usb_due)
        owed |= 1u;                             /* 2 kHz: all USB SIE traffic lives here */
#if FELUCCA_UART
    if (sub % 5u == 2u)
        owed |= 2u;                             /* 2 kHz: <= ~7 bytes per call at 31250 baud */
#endif
    if (++sub == 10u)
        sub = 0;
#if FELUCCA_UAC
    {
        static uint32_t last_ua;
        uint32_t gap = t0 - last_ua;
        if (!last_ua || gap >= 250u * FM1_TICKS_PER_US) {
            if (last_ua && gap > ua.poll_max_ticks)
                ua.poll_max_ticks = gap;
            last_ua = t0;
            ua_service();                       /* at most 4 kHz, independent of coalesced ticks */
            gap = fm1_ticks() - t0;
            if (gap > ua.service_max_ticks)
                ua.service_max_ticks = gap;
        }
    }
#endif
    if (felucca_dbg.in_audio) {
        felucca_dbg.nested++;
        t5_nested_ticks += fm1_ticks() - t0;
        return;
    }
    if (owed & 1u)
        usb_poll();
#if FELUCCA_UART
    if (owed & 2u)
        uart_midi_poll();
#endif
    owed = 0;
}
extern void isr_timer5(void);

static void timer5_start(void)                 /* OSC /4 = 6 MHz, PRD 600 -> 10 kHz */
{
    fm1_timer5_start(isr_timer5, 4);   /* above ALNK0 (3): the scan nests into the render (see fm1_timer5_irq) */
}

static void hexs(char *b, uint32_t v)
{
    uint32_t i;
    for (i = 0; i < 8u; i++)
        b[i] = "0123456789ABCDEF"[(v >> (28u - 4u * i)) & 15u];
    b[8] = 0;
}

static void fm1_fault(const fm1_crash_t *c)
{
    char b[12];
    uint32_t t0;
    fm1_audio_stop();
    lcd_fill(0, 0, 240, 240, UI_CRASH_BG);            /* fixed, outside the palettes */
    draw_text_line(0, 8, 240, &AF_M, "FELUCCA CRASH", UI_CRASH_INK, UI_CRASH_BG, 1);
    hexs(b, c->vec);
    draw_text_line(10, 40, 220, &AF_M, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    hexs(b, c->pc);
    draw_text_line(10, 60, 220, &AF_M, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    hexs(b, c->emu);
    draw_text_line(10, 84, 220, &AF_M, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    hexs(b, c->dbg);
    draw_text_line(10, 102, 220, &AF_M, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    hexs(b, c->rets);
    draw_text_line(10, 120, 220, &AF_M, b, UI_CRASH_INK, UI_CRASH_BG, 0);
    t0 = fm1_ticks();
    while ((uint32_t)(fm1_ticks() - t0) < 4000u * 1000u * FM1_TICKS_PER_US)
        ;
    fm1_reboot();
}

/* power-on: the parts with their default sounds (TRK_DEF); the sequencers empty */
static void felucca_init(void)
{
    uint32_t i;
    chain_defaults(&chain_config);
    for (i = 0; i < G_COUNT; i++)
        song.g[i] = GP[i].def;
    undo_depth++;                             /* (no undo copy of the power-on loads) */
    fm6_init();                               /* every track's FM6 patch: the init voice */
    for (i = 0; i < NTRK; i++) {
        track_t *t = &trk[i];
        track_defaults(t);
        set_engine_of(t, TRK_DEF[i][0]);
        apply_preset_to(t, TRK_DEF[i][1]);    /* with its sends */
        t->engine = t->eng_req;
        track_defaults_steps(t);              /* (a sound load never touches them) */
        if (TRK_DEF[i][2])
            load_pat16(t, PATTERNS[TRK_DEF[i][2] - 1u].note, PATTERNS[TRK_DEF[i][2] - 1u].flags);
        pat_sig[i] = steps_sig(t);            /* a default pattern, not the user's */
        pat_last[i] = TRK_DEF[i][2];
    }
    undo_depth--;
    song.sel = 0;
    song.master_q12 = 2048;
    ui.home = 1;
    ui.force = 1;
}

/* the power-on splash: with the screens in the unit (choralroot.c) ChoralRoot's (cr_shim.c cr_splash: the idle
 * stripes sliding in, the version under them); otherwise (felucca.c) plain text on gfx.c */
static void splash(void)
{
#ifdef CR_A_STRIPES
    cr_splash();
#else
    lcd_fill(0, 0, 240, 240, T_BG);
    draw_text_box(0, 94, 240, &AF_L, "CHORALROOT", T_THEME, 1);
    draw_text_box(0, 134, 240, &AF_S, FELUCCA_VERSION, T_MID, 1);
#endif
}

#ifdef CR_A_STRIPES
/* OCT- + OCT+ held at power-on: Felucca's HARDWARE CALIBRATION (ui_input.c panel_setup, which the ChoralRoot unit
 * does not include) runs on ChoralRoot's screens, from the UI frame (cr_ui.c cu_calib_*: press each printed button,
 * turn each knob right, OCT+ keeps, 30 s idle cancels); it saves the table itself (settings_save). */
static void cr_panel_setup(void) { cc.req = 1; }
#define panel_setup cr_panel_setup
#endif

/* the diagnostics record (audio.c, .noinit): what the last run left, before this boot's breadcrumbs overwrite it */
static void dbg_boot(void)
{
    if (felucca_dbg.magic != DBG_MAGIC) {
        memset(&felucca_dbg, 0, sizeof felucca_dbg);
        felucca_dbg.magic = DBG_MAGIC;
    }
    felucca_dbg.boots++;
    felucca_dbg.max_us = 0;
    felucca_dbg.in_audio = 0;                       /* .noinit: a reset inside the audio ISR left it set, and
                                                       TIMER5 would treat every tick as nested (no USB poll) */
    felucca_dbg.prev_stage = felucca_dbg.stage;     /* a WDT reset leaves the last breadcrumb here */
    felucca_dbg.prev_page = felucca_dbg.page;
    felucca_dbg.prev_home = felucca_dbg.home;
    felucca_dbg.prev_frames = felucca_dbg.ui_frames;
    felucca_dbg.prev_rst = fm1_boot.p3_rst;
    CR_STAGE(BS_GUARD);
}

static void fm1_main(void)
{
    int32_t knob = 512 * 16;
    persist_boot();                                     /* (stages BS_FLASH .. BS_SETTINGS; SAFE MODE: no object read) */
#if FELUCCA_OTA
    CR_STAGE(BS_OTA_CLEANUP);
    if (flash_ok)
        ota_boot_cleanup();                             /* staging area left by an update (also in SAFE MODE: the
                                                           SPL would re-enter the loader) */
#endif
    settings_init();
    CR_STAGE(BS_LCD);
    lcd_init();
    CR_STAGE(BS_SPLASH);
    splash();
    CR_STAGE(BS_INPUT);
    fm1_input_init();
    fm1_adc_init();
    panel_init();
    CR_STAGE(BS_SOUNDS);
    felucca_init();
    CR_STAGE(BS_AUDIO);
    audio_init();
    CR_STAGE(BS_USB);
    usb_start();
#if FELUCCA_UART
    CR_STAGE(BS_UART);
    uart_midi_init();
#endif
    CR_STAGE(BS_IRQ);
    timer5_start();
    fm1_guard_lock_top();
    fm1_irq_enable_all();
    fm1_delay_ms(30);
    CR_STAGE(BS_CALIB);
    if ((fm1_in.buttons & 3u) == 3u) {
        panel_setup();                        /* OCT- + OCT+ held at power-on */
        settings_save();
    }
    fm1_delay_ms(cr_safe ? 2500 : 400);       /* (SAFE MODE: its screen stays readable) */
    lcd_fill(0, 0, 240, 240, T_BG);

    for (;;) {
        uint32_t m = fm1_ms;
        fm1_wdt_feed();
        usb_retry(fm1_ms);
#if FELUCCA_UAC
        if (ua_off_apply(fm1_ms))                       /* Options > USB Audio Out / In: the host re-reads (a second off
                                                         * the bus; the setting is saved as any other, cr_settings.c) */
            ui_message("USB RECONNECTING");
#endif
        if (fm1_ms > BG_SETTLE_MS && bootguard.pending)  /* a crash or hang in the first 30 s counts */
            bootguard_settled(&bootguard);              /* (SAFE MODE stays for this session: cr_safe) */
        {
            int32_t b = fm1_adc_read(FM1_ADC_BATT);     /* battery: slow IIR */
            if (b > 0)
                song.batt_raw = song.batt_raw ? song.batt_raw + (b - song.batt_raw) / 32 : b;
        }
        {
            int32_t a = fm1_adc_read(FM1_ADC_MASTER);
            if (a >= 0) {
                uint32_t k10;
                knob += (a * 16 - knob) / 8;
                k10 = (uint32_t)(knob / 16);
                song.master_q12 = (k10 * k10) >> 8;            /* 0 .. ~4096 */
            }
        }
        {   /* OCT- + OCT+ held 5 s: enter UBOOT with RAM intact (debug / update); a countdown
             * shows from 2 s over the whole screen (the menu and the dialogs too: ui_draw), letting
             * go cancels it */
            static uint32_t t0;
            uint32_t both = (1u << panel.btn[B_OCTDN]) | (1u << panel.btn[B_OCTUP]);
            if ((fm1_in.buttons & both) != both) {
                if (ui.uboot) {
                    ui.uboot = 0;
                    ui.force = 1;                       /* what the countdown covered */
                    ui_message("UPDATE CANCELLED");
                }
                t0 = fm1_ms;
            } else if (fm1_ms - t0 > 2000u && fm1_ms - t0 <= 5000u) {
                uint32_t left = (5000u - (fm1_ms - t0) + 999u) / 1000u;
                if (left != ui.uboot) {
                    ui.uboot = (uint8_t)left;
                    ui.force = 1;
                }
            } else if (fm1_ms - t0 > 5000u) {
                fm1_audio_stop();
                lcd_fill(0, 0, 240, 240, T_BG);
                draw_text_box(0, 110, 240, &AF_M, "UBOOT", T_THEME, 1);
                usb_detach();
                fm1_delay_ms(30);
                bootguard_intentional(&bootguard);      /* intentional reset: not a failed boot */
                fm1_enter_uboot();
            }
        }
#if FELUCCA_OTA
        ed_service();                                   /* web editor SysEx */
        ota_service();                                  /* M-UPGRADE handshake */
        if (usb.ota_req) {                              /* M-UPGRADE upgrade command */
            usb.ota_req = 0;
            panic_req = (1u << NTRK) - 1u;               /* every track (bit per track) */
            if (flash_ok)
                ota_session();                          /* returns only if nothing was committed */
            lcd_fill(0, 0, 240, 240, T_BG);
            ui.force = 1;
        }
#endif
        if (usb.uboot_req) {                            /* SysEx F0 22 24 35 7D F7 from the host */
            fm1_audio_stop();
            lcd_fill(0, 0, 240, 240, T_BG);
            draw_text_box(0, 110, 240, &AF_M, "UBOOT (USB)", T_THEME, 1);
            fm1_delay_ms(20);
            usb_detach();
            fm1_delay_ms(30);
            bootguard_intentional(&bootguard);
            fm1_enter_uboot();
        }
#if FELUCCA_CDC
        cdc_task();
#endif
        felucca_dbg.ui_frames++;
        felucca_dbg.page = ui.page;
        felucca_dbg.home = ui.home;
        felucca_dbg.stage = BS_LOOP_INPUT;            /* (the first: cr_ui_init, BS_UI_INIT ..) */
        ui_input();
        settings_poll();                              /* queued settings save: only while stopped */
        felucca_dbg.stage = BS_LOOP_DRAW;
        ui_leds();
        ui_draw();
        felucca_dbg.stage = BS_LOOP_IDLE;
        while (fm1_ms - m < 15u) {                               /* ~60 UI frames/s at most */
            ui_input();
#if FELUCCA_OTA
            ed_service();                       /* editor replies without waiting for the next frame */
#endif
        }
    }
}

void fm1_cstart(void)
{
    uint32_t *s, *d, p3, src, wdt, mode;
    fm1_time_init();
    fm1_reset_reason();
    p3 = fm1_boot.p3_rst;
    src = fm1_boot.rst_src;
    wdt = fm1_boot.wdt_con;
    fm1_wdt_arm(0x0D);
    /* the boot guard (cr_bootguard.h): power-on clears it, watchdog / soft resets within 30 s of a boot count;
     * 2 -> SAFE MODE, 4 -> UBOOT (the record cleared: a power cycle leaves ROM boot to a clean guard) */
    mode = bootguard_step(&bootguard, bootguard_reason(p3, src));
    if (mode == BOOT_UBOOT)
        fm1_enter_uboot();
    fm1_irq_init();
    for (d = _bss_start; d < _bss_end; d++)
        *d = 0;
    for (d = _pool_start; d < _pool_end; d++)
        *d = 0;
    for (s = _data_load, d = _data_start; d < _data_end; s++, d++)
        *d = *s;
    for (s = _rt_load, d = _rt_start; d < _rt_end; s++, d++)
        *d = *s;                                /* flash driver code that must run from RAM */
    fm1_mailbox_clear();
    fm1_guard_enable(FM1_GUARD_STACK | FM1_GUARD_WRITE | FM1_GUARD_BUS | FM1_GUARD_PC);
    fm1_boot.p3_rst = (uint8_t)p3;
    fm1_boot.rst_src = src;
    fm1_boot.wdt_con = (uint8_t)wdt;
    cr_safe = mode == BOOT_SAFE;                /* (.bss: set after the clear above) */
    dbg_boot();
    fm1_main();
    for (;;)
        ;
}
