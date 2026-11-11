/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the names main.c (kept unedited) calls of Felucca's dropped UI (ui.c, ui_input.c, ui_draw.c,
 * project.c, editor.c), with ChoralRoot's meaning (docs/INTEGRATION.md section 1). Device unit only (choralroot.c);
 * the emulator calls cr_ui_* itself (tools/emu/emu_fw.c).
 *
 *   felucca_init (main.c's power-on) -> track_defaults / set_engine_of / apply_preset_to: the parts get a sound
 *     (Felucca's TRK_DEF; ChoralRoot's own sounds replace them at the first frame); the step / pattern calls: none
 *   the main loop: ui_input -> cr_ui_input (the first call: cr_ui_init, the engine's power-on), ui_leds ->
 *     cr_ui_frame, ui_draw -> cr_ui_draw (the update gesture's countdown, OCT- + OCT+ + HOME held, is a big message
 *     of cr_backup.c cr_update_poll)
 *   persist_boot: the flash part (the user sample sets with FELUCCA_SAMPLE only), the user sounds and the settings
 *     record; settings_save / settings_poll: cr_settings.c; panel_setup: main.c's own (cr_panel_setup); ed_service:
 *     cr_backup.c's (backup and restore over SysEx: Felucca's protocol, a subset; editor.c is dropped) */

/* ---------------------------------------------------------- ui.c's state --- */
/* (`ui`, ui_message and load_pat16: cr_bank.c / cr_ui.c, which upreset.c needs in the emulator too) */
static uint32_t undo_depth;            /* (main.c: no undo copy of the power-on loads; ChoralRoot keeps none) */
static uint32_t pat_sig[NTRK];
static uint8_t pat_last[NTRK];
static uint8_t cr_booted;


/* ---------------------------------------------------------- the power-on --- */
static void track_defaults(track_t *t)
{
    uint32_t i;
    for (i = 0; i < P_E0; i++)
        t->p[i] = TP[i].def;
}
static void track_defaults_steps(track_t *t) { (void)t; }
static void set_engine_of(track_t *t, uint32_t ei)
{
    const engine_t *e = ENGINES[ei % NENGINES];
    uint32_t i;
    t->eng_req = (uint8_t)(eng_ok(ei % NENGINES) ? ei % NENGINES : ENGI_FM6);
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = e->edit[i].def;
}
static void apply_preset_to(track_t *t, uint32_t pi) { cu_load(t, t->eng_req, pi, t == &trk[CR_PART_BASS]); }
static uint32_t steps_sig(const track_t *t) { (void)t; return 0; }

#if CR_RES
/* the resource region (cr_res.h): this build's? Once, after fl_plain_window_init; cr_res_ok stays 0 otherwise (a cut
 * install the stub did not catch, a region another firmware erased, a unit whose flash is not the expected part) */
extern const uint8_t _res_start[], _res_end[];   /* firmware/app.ld .res: XIP FL_RES_DATA .. */
static void cr_res_boot(void)
{
    const cr_res_hdr_t *h = (const cr_res_hdr_t *)fm1_xip_ptr(FL_RES_LO);
    uint32_t n = (uint32_t)(_res_end - _res_start);
    cr_res_ok = (uint8_t)((uint32_t)(uintptr_t)_res_start == FM1_XIP(FL_RES_DATA) && n <= FL_RES_HI - FL_RES_DATA &&
                          h->magic == CR_RES_MAGIC && h->format == CR_RES_FORMAT && h->hcrc == st_crc32(h, 60u) &&
                          h->len == n && fm1_xip_rd32(CR_RES_MARK_OFF) == CR_RES_MARK_MAGIC &&   /* (the app area:
                                                                                                  * read decrypted) */
                          h->body_crc == fm1_xip_rd32(CR_RES_MARK_OFF + 12u) && st_crc32(_res_start, n) == h->crc);
}
#endif
#if CR_RES2
/* the second region (cr_res.h CR_RES2, HISTORY 3y296): the same checks under its own magic; its data is this build's
 * .res2 (firmware/app.ld) */
extern const uint8_t _res2_start[], _res2_end[];   /* XIP FL_RES2_DATA .. */
static void cr_res2_boot(void)
{
    const cr_res_hdr_t *h = (const cr_res_hdr_t *)fm1_xip_ptr(FL_RES2_LO);
    uint32_t n = (uint32_t)(_res2_end - _res2_start);
    cr_res2_ok = (uint8_t)((uint32_t)(uintptr_t)_res2_start == FM1_XIP(FL_RES2_DATA) && n <= FL_RES2_HI - FL_RES2_DATA &&
                           h->magic == CR_RES2_MAGIC && h->format == CR_RES_FORMAT && h->hcrc == st_crc32(h, 60u) &&
                           h->len == n && fm1_xip_rd32(CR_RES_MARK_OFF) == CR_RES_MARK_MAGIC &&
                           h->body_crc == fm1_xip_rd32(CR_RES_MARK_OFF + 12u) && st_crc32(_res2_start, n) == h->crc);
}
#endif

static void persist_boot(void)         /* before settings_init / panel_init (project.c's, without the stores) */
{
#if FELUCCA_FLASH
    uint32_t f = irq_save(), k;
    uint8_t uid[16];
    int uid_rc = -1;
    CR_STAGE(BS_FLASH);
    flash_ok = FL_JEDEC_OK(FL_FAR(fl_jedec_ram)());      /* the expected 1 MiB part, else stay RAM-only */
    if (flash_ok && !cr_safe)                              /* its unique ID (fm1_flash.h fl_uid_ram: the SDK's own
                                                            * read, nothing written), for the unit's ID; not in SAFE
                                                            * MODE: that boot runs as it did before the ID came */
        uid_rc = FL_FAR(fl_uid_ram)(uid);
    irq_restore(f);
    if (!flash_ok)
        return;
    if (!uid_rc)
        cr_unit_id_set(uid);
    fl_plain_window_init();                                /* the data region reads as plaintext through XIP */
#if CR_RES
    cr_res_boot();                                         /* the resource region: this build's? (cr_res.h) */
#endif
#if CR_RES2
    cr_res2_boot();                                        /* the second region: this build's? (before cr_bank_boot:
                                                            * the pool's names are the presets') */
#endif
#if BOARD_SMK37
    if (!cr_safe)
        st_owner_check();                                  /* SMK: never parse another ported firmware's store
                                                            * (storage.c); SAFE MODE writes nothing */
#endif
    /* SAFE MODE (main.c, cr_bootguard.h): flash_ok stays (the installer's update and the backup's reads need it),
     * but no object is read: storage.c st_load / st_save and the loop slots answer "none" (core.h ST_BLOCKED), so
     * the sounds, the stores, the FM6 bank and the settings start as on an erased flash; the flash is not touched */
    (void)k;                                               /* (no SAMPLE engine: no user sample sets to scan) */
    cr_bank_boot();                                        /* the 32 user sounds (upreset.c) and the FM6 bank */
#if CR_HAVE_SETTINGS
    CR_STAGE(BS_SETTINGS);
    cr_settings_boot();                                    /* the settings record with ChoralRoot's block */
    crs_ahead_boot();                                      /* the quiet writes: the spare copies erased now, before
                                                            * the audio starts (cr_settings.c; SAFE MODE: nothing) */
#endif
#endif
}
#if CR_HAVE_SETTINGS
static void settings_poll(void)                    /* saved on change, deferred while a loop plays (SAFE MODE: never) */
{
    if (!cr_safe)
        cr_settings_poll();
}
static void settings_save(void)
{
    if (!cr_safe)
        cr_settings_save();
}
#else
static void settings_poll(void) {}
static void settings_save(void) {}
#endif
#ifndef CR_BACKUP_SERVICE
static void ed_service(void) {}        /* (no SysEx service: cr_backup.c's is the device's, FELUCCA_OTA) */
#endif

/* ------------------------------------------------------- the power-on splash --- */
/* main.c's splash(): the idle stripes (cr_ui.c cu_stripes) sliding in with the version under them, played here
 * while main.c boots (IRQs still off: a busy-wait clock). Options > Motion from the record persist_boot read:
 * Off draws the settled picture, Calm plays it at double speed. The main loop's idle screen then continues it
 * (cu_intro_played: no second slide; the version stays CR_SPLASH_MS) */
#define CR_SPLASH_PLAY_MS 480u
static void cr_splash(void)
{
    cr_screen_t *s = &cu_scr;                      /* (cr_ui.c's frame, in the pool: 2.4 KB off the boot stack and
                                                    * out of RAM; the UI's first frame clears it) */
    uint32_t t, mo = CR_MOTION_FULL;
#if CR_HAVE_SETTINGS
    if (crs_booted && crs_rec.cr.motion < CR_MOTION_N)
        mo = crs_rec.cr.motion;
#endif
    palette_set(NPALETTES - 1u);                   /* MOD (cr_ui_init sets it again, then the saved one) */
    if (cr_safe) {                                 /* SAFE MODE: its screen (cr_ui.c cu_safe_screen), no slide */
        cr_screen_clear(s);
        cu_safe_screen(s);
        cr_draw_invalidate();
        cr_draw(s, CR_ANIM_SETTLED);
        cr_draw_invalidate();
        return;
    }
    cr_screen_clear(s);
    cu_stripes(s);
    s->batt = 255;
    cu_cpy(s->foot, CR_VERSION, sizeof s->foot);
    s->anim = CR_A_INTRO;
    cr_draw_invalidate();
    for (t = 0; mo != CR_MOTION_OFF && t < CR_SPLASH_PLAY_MS; t += 16u) {
        cr_draw(s, mo == CR_MOTION_CALM ? t * 2u : t);
        fm1_wdt_feed();
        fm1_delay_ms(16);
    }
    cr_draw(s, CR_ANIM_SETTLED);
    cu_intro_played = 1;
#if CR_RES
    if (!cr_res_ok || !cr_res2_ok) {               /* a region is not this build's: the screens lack their big type
                                                    * (cr_res.h), or the factory presets their lists (the second);
                                                    * say so in S, which is in the image, for 3 s */
        uint32_t w;
        lcd_fill(0, 196, 240, 44, T_BG);
        draw_text_box(0, 200, 240, &AF_S, !cr_res_ok ? "FONTS MISSING" : "PRESETS MISSING", T_THEME, 1);
        draw_text_box(0, 218, 240, &AF_S, "INSTALL CHORALROOT AGAIN", T_TEXT, 1);
        for (w = 0; w < 3000u; w += 10u) {
            fm1_wdt_feed();
            fm1_delay_ms(10);
        }
    }
#endif
    cr_draw_invalidate();                          /* the UI's first frame draws everything */
}

/* ------------------------------------------------------------ the frame --- */
static void ui_input(void)
{
    if (!cr_booted) {                  /* the first scan: ChoralRoot's power-on (the engine, its sounds) */
        cr_ui_init();
        cr_booted = 1;
    }
    cr_ui_input();
}
static void ui_leds(void)
{
    if (cr_booted)
        cr_ui_frame();
}
static void ui_draw(void)
{
    if (!cr_booted)
        return;
    if (ui.force) {
        ui.force = 0;
        cr_draw_invalidate();
    }
    cr_ui_draw();                      /* (the update gesture's countdown: cr_backup.c cr_update_poll's message) */
}
