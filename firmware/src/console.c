/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Serial console on the CDC-ACM function (FELUCCA_CDC=1): read-only
 * diagnostics. Runs in the main loop (cdc_task); usb_poll moves the bytes.
 * The baud rate is ignored. Nothing here writes memory or flash; `uboot`
 * does what the SysEx soft key does. */
#define CON_LINE 64u

static struct {
    char line[CON_LINE];
    uint32_t len;
    uint8_t dtr_seen, stalled;   /* stalled: the host stopped reading, drop the rest of this reply */
} con;

static void con_putc(char c)
{
    uint32_t t0 = fm1_ms;
    while (co_w - co_r >= CO_N) {                      /* full: wait for usb_poll, briefly */
        if (!cdc.dtr || con.stalled || fm1_ms - t0 > 20u) {
            con.stalled = 1;
            return;
        }
        fm1_wdt_feed();
    }
    cdc_out[co_w % CO_N] = (uint8_t)c;
    RING_PUBLISH();
    co_w++;
}

static void con_puts(const char *s)
{
    while (*s)
        con_putc(*s++);
}

static void con_hex(uint32_t v, uint32_t digits)
{
    while (digits--)
        con_putc("0123456789ABCDEF"[(v >> (digits * 4u)) & 15u]);
}

static void con_dec(int32_t v)
{
    char b[12];
    uint32_t n = 0, u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    if (v < 0)
        con_putc('-');
    do
        b[n++] = (char)('0' + u % 10u);
    while ((u /= 10u) != 0 && n < sizeof b);
    while (n)
        con_putc(b[--n]);
}

static void con_kv(const char *k, int32_t v)            /* "key value\r\n" */
{
    con_puts(k);
    con_putc(' ');
    con_dec(v);
    con_puts("\r\n");
}

static void con_kx(const char *k, uint32_t v)
{
    con_puts(k);
    con_puts(" 0x");
    con_hex(v, 8);
    con_puts("\r\n");
}

static uint32_t con_num(const char **p, int *ok)        /* decimal or 0x hex */
{
    const char *s = *p;
    uint32_t v = 0, base = 10, d, n = 0;
    while (*s == ' ')
        s++;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X'))
        s += 2, base = 16;
    for (;; s++, n++) {
        char c = *s;
        if (c >= '0' && c <= '9')
            d = (uint32_t)(c - '0');
        else if (base == 16 && c >= 'a' && c <= 'f')
            d = (uint32_t)(c - 'a' + 10);
        else if (base == 16 && c >= 'A' && c <= 'F')
            d = (uint32_t)(c - 'A' + 10);
        else
            break;
        v = v * base + d;
    }
    *ok = n > 0;
    *p = s;
    return v;
}

static int con_word(const char **p, const char *w)       /* match a whole word */
{
    const char *s = *p;
    while (*s == ' ')
        s++;
    while (*w && *s == *w)
        s++, w++;
    if (*w || (*s && *s != ' '))
        return 0;
    *p = s;
    return 1;
}

static void con_memr(const char *p)
{
    int ok, ok2;
    uint32_t a = con_num(&p, &ok), n = con_num(&p, &ok2), i;
    if (!ok) {
        con_puts("usage: memr ADDR [LEN<=256]\r\n");
        return;
    }
    if (!ok2 || !n)
        n = 64;
    if (n > 256u)
        n = 256;
    if (!fm1_mem_readable(a, n)) {
        con_puts("only RAM 01C00000-01C80000 and XIP 02000000-02100000\r\n");
        return;
    }
    for (i = 0; i < n; i++) {
        if (i % 16u == 0) {
            con_hex(a + i, 8);
            con_putc(':');
        }
        con_putc(' ');
        con_hex(fm1_peek8(a + i), 2);
        if (i % 16u == 15u || i + 1u == n)
            con_puts("\r\n");
    }
}

#if FELUCCA_FLASH
static void con_flr(const char *p)                  /* flash read over SPI (no XIP decryption) */
{
    static uint8_t b[256];
    int ok, ok2;
    uint32_t a = con_num(&p, &ok), n = con_num(&p, &ok2), i;
    if (!ok || a < 0x93000u || a >= 0x100000u) {
        con_puts("usage: flr OFFSET [LEN<=256]   (0x93000..0xFFFFF)\r\n");
        return;
    }
    if (!ok2 || !n)
        n = 64;
    if (n > 256u)
        n = 256;
    if (a + n > 0x100000u)
        n = 0x100000u - a;
    if (!flash_ok || st_read(a, b, n)) {
        con_puts("flash not available\r\n");
        return;
    }
    for (i = 0; i < n; i++) {
        if (i % 16u == 0) {
            con_hex(a + i, 6);
            con_putc(':');
        }
        con_putc(' ');
        con_hex(b[i], 2);
        if (i % 16u == 15u || i + 1u == n)
            con_puts("\r\n");
    }
}
#endif

#if FELUCCA_UAC
static void con_uac(void)                              /* USB audio (usb_audio_stream.c): the recording and its
                                                        * glitches (the console is presented only with USB Record
                                                        * Off: the counters of the last stream, since the boot) */
{
    con_kv("ua_off", ua_off);
    con_kv("ua_cap_alt", ua.cap_alt);
    con_kv("ua_tx_pkts", (int32_t)ua.tx_packets);
    con_kv("ua_cap_underruns", (int32_t)ua.cap_underruns);
    con_kv("ua_cap_overruns", (int32_t)ua.cap_overruns);
    con_kv("ua_missed_frames", (int32_t)ua.missed_frames);
    con_kv("ua_poll_max_us", (int32_t)(ua.poll_max_ticks / FM1_TICKS_PER_US));
    con_kv("ua_service_max_us", (int32_t)(ua.service_max_ticks / FM1_TICKS_PER_US));
}
#endif

static void con_status(void)
{
    const engine_t *e = ENGINES[TSEL->eng_req % NENGINES];
    con_puts("felucca ");
    con_puts(FELUCCA_VERSION);
    con_puts("\r\n");
    con_kv("uptime_ms", (int32_t)fm1_ms);
    con_kv("cpu_pct", (int32_t)(song.cpu_q8 * 100u / 256u));
    con_kv("audio_max_us", (int32_t)felucca_dbg.max_us);
    con_kv("audio_late", (int32_t)felucca_dbg.late);
    con_kv("voices_shed", (int32_t)shed_count);
    con_kv("voices_given_up", (int32_t)voice_kills);
    con_kv("track", (int32_t)song.sel + 1);
    con_kv("batt_raw", song.batt_raw);
    con_puts("engine ");
    con_puts(e->name);
    con_puts("\r\n");
    con_puts("preset ");
    con_puts(TSEL->preset < e->npresets ? e->presets[TSEL->preset].name : "-");
    con_puts("\r\n");
    con_kv("bpm", song.g[G_BPM]);
    con_kv("playing", song.playing);
    con_kv("boots", (int32_t)felucca_dbg.boots);
    con_kv("usb_resets", (int32_t)usb.resets);
    con_kv("usb_sof", (int32_t)usb.sof_seen);
    con_kv("usb_suspends", (int32_t)usb.suspends);
    con_kv("usb_retries", (int32_t)usb.retries);
    con_kv("usb_frame", (int32_t)usb.frame);
    con_kv("usb_frame_stalls", (int32_t)usb.frame_stalls);
    con_kv("usb_max_gap_polls", (int32_t)usb.max_gap);
    con_kv("midi_rx_pkts", (int32_t)usb.rx_pkts);
    con_kv("midi_tx_pkts", (int32_t)usb.tx_pkts);
#if FELUCCA_UAC
    con_uac();
#endif
    con_kv("uart_enabled", FELUCCA_UART);
#if FELUCCA_UART
    con_kv("uart_rx_bytes", (int32_t)um.bytes);
    con_kv("uart_rx_msgs", (int32_t)um.msgs);
    con_kv("uart_rx_drops", (int32_t)um.drops);
#endif
#if FELUCCA_FLASH
    con_kv("flash", flash_ok);
#endif
}

/* the audio ISR's last second (audio.c cpu_window) and what cuts the sound (docs/INTEGRATION.md, Performance) */
static void con_cpu(void)
{
    con_kv("audio_budget_us", (int32_t)(HALF_FRAMES * 1000000u / FS));
    con_kv("audio_avg_us", (int32_t)cpu_last.avg_us);         /* render, TIMER5 nested left out */
    con_kv("audio_max_us", (int32_t)cpu_last.max_us);
    con_kv("audio_max_all_us", (int32_t)cpu_last.max_all_us); /* render + nested: what the DMA deadline sees */
    con_kv("audio_halves", (int32_t)cpu_last.halves);
    con_kv("audio_late", (int32_t)cpu_last.late);              /* halves the DMA moved past (overruns) */
    con_kv("audio_late_total", (int32_t)felucca_dbg.late);
    con_kv("audio_max_us_total", (int32_t)felucca_dbg.max_us);
    con_kv("cpu_pct", (int32_t)(song.cpu_q8 * 100u / 256u));
    con_kv("voices_given_up", (int32_t)voice_kills);          /* faded for another note / part, or shed */
    con_kv("voices_stolen", (int32_t)voice_steals);           /* a part's own voice taken for a new note */
    con_kv("voices_shed", (int32_t)shed_count);               /* overload: two halves above 85 % */
#if FELUCCA_FLASH
    con_kv("flash_erases", (int32_t)st_erases);               /* each: ~45 ms of silence, IRQs off */
#endif
#ifdef CR_VERSION
    con_kv("settings_saves", (int32_t)crs_saves);
#endif
    con_kv("ui_frame_max_ms", (int32_t)cpu_last.ui_max_ms);   /* the longest main-loop frame (15 ms nominal) */
    con_kv("ui_frames", (int32_t)cpu_last.ui_frames);
#if CR_CPU1
    {   /* the ISR's stages, the last second (cr_cpu1.h): per half, us; and the second core (docs/DUALCORE.md) */
        uint32_t k;
        char nm[24];
        for (k = 0; k < CRP_N; k++) {
            uint32_t l;
            str_cpy(nm, "stage_", sizeof nm);
            str_cpy(nm + 6, CRP_NAME[k], sizeof nm - 6u);
            l = (uint32_t)str_len(nm);
            str_cpy(nm + l, "_avg_us", sizeof nm - l);
            con_kv(nm, (int32_t)cr_dbg.avg_us[k]);
            str_cpy(nm + l, "_max_us", sizeof nm - l);
            con_kv(nm, (int32_t)cr_dbg.max_us[k]);
        }
        con_kv("c1_state", (int32_t)cr_dbg.c1);                /* 0 off, 1 ok, 2 failed at start, 3 gave up on a job */
        con_kv("c1_jobs", (int32_t)cr_dbg.c1_jobs);
        con_kv("c1_stack_bytes", (int32_t)cr_dbg.c1_stack);    /* of 2816 */
        con_kv("c1_start_us", (int32_t)cr_dbg.c1_start_us);
    }
#endif
}

static void con_dbg(void)
{
    const uint32_t *w = (const uint32_t *)&felucca_dbg;
    static const char *const NAMES[] = {"magic", "halves", "max_us", "nested", "in_audio", "late",
                                        "timer_irqs", "ui_frames", "last_us", "cpu_q8", "boots",
                                        "stage", "page", "home", "prev_stage", "prev_page",
                                        "prev_home", "prev_rst", "prev_frames"};
    uint32_t i;
    for (i = 0; i < sizeof NAMES / sizeof NAMES[0] && i < sizeof felucca_dbg / 4u; i++)
        con_kx(NAMES[i], w[i]);
#if FELUCCA_UAC
    con_uac();
#endif
}

/* input scan (hal/fm1_input.h fm1_in_stat) since the last `inp`, then cleared: tick gaps (= LED
 * on-time of a column), per-column LED share in 1/1000, the tick cost, the dim pulse (its average width and the
 * bits of the 595 shift it spans), encoder transitions */
static void con_inp(void)
{
    static uint32_t t_last;
    uint32_t now = fm1_ticks(), win = now - t_last, i, k;
    uint32_t win_us = win / FM1_TICKS_PER_US;
    t_last = now;
    con_kv("window_ms", (int32_t)(win_us / 1000u));
    con_kv("ticks_per_s", (int32_t)(win_us >= 1000u ? fm1_in_stat.ticks * 1000u / (win_us / 1000u) : 0u));
    con_kv("gap_max_us", (int32_t)(fm1_in_stat.gap_max / FM1_TICKS_PER_US));
    con_puts("gap_hist <.15 .3 .6 1.2 2.4 4.8 9.6 more ms:");
    for (i = 0; i < FM1_GAP_BINS; i++) {
        con_putc(' ');
        con_dec((int32_t)fm1_in_stat.gap_hist[i]);
    }
    con_puts("\r\n");
    con_kv("tick_cost_avg_ns", (int32_t)(fm1_in_stat.ticks ? fm1_in_stat.cost_sum / fm1_in_stat.ticks * 1000u / FM1_TICKS_PER_US : 0u));
    con_kv("tick_cost_max_us", (int32_t)(fm1_in_stat.cost_max / FM1_TICKS_PER_US));
    con_kv("tick_cpu_permille", (int32_t)(win >= 1000u ? fm1_in_stat.cost_sum / (win / 1000u) : 0u));
    con_kv("dim_pulse_ns", (int32_t)(fm1_in_stat.dim_n ? fm1_in_stat.dim_sum / fm1_in_stat.dim_n * 1000u / FM1_TICKS_PER_US : 0u));
    con_kv("dim_bits", (int32_t)fm1__dim_k);
    con_puts("led_on_permille:");
    for (i = 0; i < FM1_NCOL; i++) {
        con_putc(' ');
        con_dec((int32_t)(win >= 1000u ? fm1_in_stat.on[i] / (win / 1000u) : 0u));
    }
    con_puts("\r\nled_on_max_us:");
    for (i = 0; i < FM1_NCOL; i++) {
        con_putc(' ');
        con_dec((int32_t)(fm1_in_stat.on_max[i] / FM1_TICKS_PER_US));
    }
    con_puts("\r\nenc moves/lost/detent state:");
    for (k = 0; k < FM1_NENC; k++) {
        con_putc(' ');
        con_dec((int32_t)fm1_in_stat.enc_moves[k]);
        con_putc('/');
        con_dec((int32_t)fm1_in_stat.enc_lost[k]);
        con_putc('/');
        con_dec(fm1_in.enc_rest[k]);
    }
    con_puts("\r\n");
    {   /* note keys, from the first scan that saw the contact (us): debounced press, note-on, DAC out */
        uint32_t n = fm1_in_stat.press_n, m = fm1_in_stat.kb_n;
        con_kv("key_presses", (int32_t)n);
        con_kv("key_press_avg_us", (int32_t)(n ? fm1_in_stat.press_sum / n / FM1_TICKS_PER_US : 0u));
        con_kv("key_press_max_us", (int32_t)(fm1_in_stat.press_max / FM1_TICKS_PER_US));
        con_kv("key_notes", (int32_t)m);
        con_kv("key_noteon_avg_us", (int32_t)(m ? fm1_in_stat.kb_sum / m / FM1_TICKS_PER_US : 0u));
        con_kv("key_noteon_max_us", (int32_t)(fm1_in_stat.kb_max / FM1_TICKS_PER_US));
        con_kv("key_dac_avg_us", (int32_t)(m ? fm1_in_stat.dac_sum / m / FM1_TICKS_PER_US : 0u));
        con_kv("key_dac_max_us", (int32_t)(fm1_in_stat.dac_max / FM1_TICKS_PER_US));
    }
    con_kv("timer_irqs", (int32_t)felucca_dbg.timer_irqs);
    con_kv("nested", (int32_t)felucca_dbg.nested);
    {
        uint32_t k2 = fm1__lock();
        memset((void *)&fm1_in_stat.gap_max, 0, sizeof fm1_in_stat - sizeof fm1_in_stat.last);
        fm1__unlock(k2);
    }
}

/* the boot guard (cr_bootguard.h, main.c): this boot's mode, the reset that started it, the breadcrumb the last run
 * left, and the stage numbers' names */
static void con_boot(void)
{
    uint32_t i;
    con_puts("mode ");
    con_puts(bootguard_mode_name(bootguard.mode));
    con_puts(cr_safe ? " (SAFE MODE: flash data skipped; Options > Flash Data erases it)\r\n" : "\r\n");
    con_kv("failed", (int32_t)bootguard.failed);
    con_kv("pending", (int32_t)bootguard.pending);
    con_puts("reset ");
    con_puts(bootguard_class_name(bootguard.cls));
    con_puts(bootguard.counted ? " (counted as a failed boot)\r\n" : "\r\n");
    con_kx("reset_reason", bootguard.reason);              /* P3_RST_SRC bits 0..7, RST_SRC bit5 as bit 8 */
    con_kx("p3_rst", fm1_boot.p3_rst);
    con_kx("rst_src", fm1_boot.rst_src);
    con_kx("wdt_con", fm1_boot.wdt_con);
    con_puts("last_stage ");
    con_dec((int32_t)felucca_dbg.prev_stage);
    con_putc(' ');
    con_puts(bootguard_stage_name(felucca_dbg.prev_stage));
    con_puts("\r\n");
    con_kv("boots", (int32_t)felucca_dbg.boots);
    con_puts("stages:");
    for (i = 1; i < BS_N; i++)
        if (i < 3u || i == BS_LOOP_IDLE || i >= BS_GUARD) {
            con_putc(' ');
            con_dec((int32_t)i);
            con_putc('=');
            con_puts(bootguard_stage_name(i));
        }
    con_puts("\r\n");
}

static void con_crash(void)
{
    if (fm1_crash.magic != FM1_CRASH_MAGIC) {
        con_puts("no crash record\r\n");
        return;
    }
    con_kv("count", (int32_t)fm1_crash.count);
    con_kx("vec", fm1_crash.vec);
    con_kx("pc", fm1_crash.pc);
    con_kx("rets", fm1_crash.rets);
    con_kx("emu", fm1_crash.emu);
    con_kx("sp", fm1_crash.sp);
    con_kx("psr", fm1_crash.psr);
    con_kv("uptime_ms", (int32_t)fm1_crash.uptime_ms);
    con_kv("early", (int32_t)fm1_crash.early);
}

static void con_params(void)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++) {
        con_dec((int32_t)i);
        con_putc('=');
        con_dec(TSEL->p[i]);
        con_puts(i % 8u == 7u || i + 1u == P_COUNT ? "\r\n" : " ");
    }
}

static void con_exec(const char *p)
{
    if (con_word(&p, "help") || con_word(&p, "?"))
        con_puts("status  cpu  dbg  boot  inp  crash  params  memr ADDR [LEN]  flr OFF [LEN]  uboot yes\r\n");
    else if (con_word(&p, "cpu"))
        con_cpu();
    else if (con_word(&p, "status"))
        con_status();
    else if (con_word(&p, "dbg"))
        con_dbg();
    else if (con_word(&p, "boot"))
        con_boot();
    else if (con_word(&p, "crash"))
        con_crash();
    else if (con_word(&p, "inp"))
        con_inp();
    else if (con_word(&p, "params"))
        con_params();
    else if (con_word(&p, "memr"))
        con_memr(p);
#if FELUCCA_FLASH
    else if (con_word(&p, "flr"))
        con_flr(p);
#endif
    else if (con_word(&p, "uboot")) {
        if (con_word(&p, "yes")) {
            con_puts("entering UBOOT\r\n");
            usb.uboot_req = 1;                         /* main loop: same path as the SysEx key */
        } else {
            con_puts("type 'uboot yes'\r\n");
        }
    } else if (*p)
        con_puts("? (help)\r\n");
}

static void cdc_task(void)                              /* main loop */
{
    if (cdc.dtr && !con.dtr_seen) {
        con_puts("\r\nFelucca ");
        con_puts(FELUCCA_VERSION);
        con_puts(" console - 'help'\r\n> ");
    }
    con.dtr_seen = cdc.dtr;
    while (ci_r != ci_w) {
        char c;
        RING_PUBLISH();                                /* usb_poll (producer) can preempt us */
        c = (char)cdc_in[ci_r % CI_N];
        RING_PUBLISH();
        ci_r++;
        if (c == '\r' || c == '\n') {
            if (c == '\n' && con.len == 0)
                continue;                              /* the LF of a CR LF */
            con_puts("\r\n");
            con.line[con.len] = 0;
            con.stalled = 0;
            con_exec(con.line);
            con.len = 0;
            con_puts("> ");
        } else if (c == 0x7F || c == 0x08) {
            if (con.len) {
                con.len--;
                con_puts("\b \b");
            }
        } else if (c >= ' ' && c < 0x7F && con.len + 1u < CON_LINE) {
            con.line[con.len++] = c;
            con_putc(c);
        }
    }
}
