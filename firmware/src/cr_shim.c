/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the names main.c (kept unedited) calls of Felucca's dropped UI (ui.c, ui_input.c, ui_draw.c,
 * project.c, editor.c), with ChoralRoot's meaning (docs/INTEGRATION.md section 1). Device unit only (choralroot.c);
 * the emulator calls cr_ui_* itself (tools/emu/emu_fw.c).
 *
 *   felucca_init (main.c's power-on) -> track_defaults / set_engine_of / apply_preset_to: the parts get a sound
 *     (Felucca's TRK_DEF; ChoralRoot's own sounds replace them at the first frame); the step / pattern calls: none
 *   the main loop: ui_input -> cr_ui_input (the first call: cr_ui_init, the engine's power-on), ui_leds ->
 *     cr_ui_frame, ui_draw -> cr_ui_draw (the UPDATE MODE countdown of OCT- + OCT+ held: a big message)
 *   persist_boot: the flash part, the user sample sets, the user sounds and the settings record; settings_save /
 *     settings_poll: cr_settings.c; panel_setup: main.c's own (cr_panel_setup); ed_service: none (the web editor's
 *     SysEx is Felucca's, dropped with editor.c) */

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

static void persist_boot(void)         /* before settings_init / panel_init (project.c's, without the stores) */
{
#if FELUCCA_FLASH
    uint32_t f = irq_save(), k;
    flash_ok = FL_FAR(fl_jedec_ram)() == 0x856014u;       /* the expected 1 MiB part, else stay RAM-only */
    irq_restore(f);
    if (!flash_ok)
        return;
    fl_plain_window_init();                                /* user sample sets play from flash through XIP */
    for (k = 0; k < SMP_USER_SLOTS; k++)
        smp_user_scan(k);
    cr_bank_boot();                                        /* the 32 user sounds (upreset.c) and the FM6 bank */
#if CR_HAVE_SETTINGS
    cr_settings_boot();                                    /* the settings record with ChoralRoot's block */
#endif
#endif
}
#if CR_HAVE_SETTINGS
static void settings_poll(void) { cr_settings_poll(); }   /* saved on change, deferred while a loop plays */
static void settings_save(void) { cr_settings_save(); }
#else
static void settings_poll(void) {}
static void settings_save(void) {}
#endif
static void ed_service(void) {}        /* (Felucca's web editor SysEx: not in ChoralRoot 0.1) */

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
    if (ui.uboot) {                    /* OCT- + OCT+ held 2..5 s (main.c): the update countdown */
        char n[2] = {(char)('0' + ui.uboot % 10u), 0};
        cu.msg.until = cu_now() + 100u;
        cu.msg.big = 1;
        cu_cpy(cu.msg.text, n, sizeof cu.msg.text);
        cu_cpy(cu.msg.label, "update mode: let go to cancel", sizeof cu.msg.label);
    }
    cr_ui_draw();
}
