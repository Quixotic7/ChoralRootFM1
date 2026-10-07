/* SPDX-License-Identifier: GPL-3.0-only */
/* The firmware side of the emulator: the firmware sources (emu_firmware.h) and the hooks of emu_hooks.h.
 * Everything Felucca-specific the emulator relies on is in this file's hook bodies. */
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
static void emu_fw_exit(void)
{
    if (!crs_loaded)
        return;
    if (emu_save_on_exit)
        cr_settings_save();
    else
        cr_settings_poll();
}

#define EMU_RELEASE_MS 9u            /* hal/fm1_input.h: a release needs ~9 ms open (FM1_DEB_RELEASE) */
static uint32_t in_btn, in_key;      /* the debounced state the tick delivers */
static uint32_t btn_ms[32], key_ms[32];/* ms each went down */
static int32_t master_knob = 512 * 16;

void emu_fw_init(int demo)
{
    struct timespec ts;
    uint32_t i;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    emu_t0_ns = (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
    if (emu_hal.ready == 2u)                      /* --bench: simulated time (set before init) */
        emu_sim = 1;

    /* power-on (choralroot.c's main.c path): the flash (persist_boot), settings, panel, the engine, the sounds */
    emu_flash_open();
    cr_bank_boot();                               /* persist_boot: the user sounds (upreset.c) from the flash */
    cr_settings_boot();                           /* .. and the settings record (Felucca's fields + ChoralRoot's) */
    cr_ui_init();                                 /* the engine, the sounds, the record applied (cr_settings_load) */
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
    if (!emu_hal.master)
        emu_hal.master = 724;                     /* -> master_q12 2047, as felucca_init */
    master_knob = emu_hal.master * 16;
    lcd_fill(0, 0, 240, 240, T_BG);
    emu_hal.ready = 1;
}

/* the 1 ms timer: what fm1_timer5_irq / fm1_input_tick give the main loop */
void emu_fw_tick(uint32_t ms)
{
    uint32_t b, k, i, down;
    fm1_irq_off();
    if (emu_sim)
        emu_sim_us = ms * 1000u;
    if ((int32_t)(ms - fm1_ms) > 0)
        fm1_ms = ms;
    b = emu_hal.buttons | __atomic_exchange_n(&emu_hal.buttons_tap, 0u, __ATOMIC_SEQ_CST);
    k = emu_hal.keys | __atomic_exchange_n(&emu_hal.keys_tap, 0u, __ATOMIC_SEQ_CST);
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
        if (down & m)
            key_ms[i] = ms;
        else if ((in_key & m) && !(k & m) && ms - key_ms[i] < EMU_RELEASE_MS)
            k |= m;
    }
    in_key = k;
    if (down)
        __atomic_fetch_or(&host_notes, down, __ATOMIC_SEQ_CST);
    fm1_in.buttons = in_btn;
    fm1_in.notes = in_key;
    for (i = 0; i < EMU_NENC; i++) {
        int32_t s = __atomic_exchange_n(&emu_hal.enc[i], 0, __ATOMIC_SEQ_CST);
        if (s)
            __atomic_fetch_add(&host_enc[i], s, __ATOMIC_SEQ_CST);
    }
    fm1_irq_on();
}

static void master_pot(void)                      /* main.c: the MASTER pot (ADC) through its IIR */
{
    int32_t a = emu_hal.master;
    uint32_t k10;
    a = a < 0 ? 0 : a > 1023 ? 1023 : a;
    master_knob += (a * 16 - master_knob) / 8;
    k10 = (uint32_t)(master_knob / 16);
    song.master_q12 = (k10 * k10) >> 8;
}

static uint32_t real_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(((uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec) / 1000u);
}

void emu_fw_frame(void)                           /* main.c's loop body (choralroot.c: the same calls) */
{
    uint32_t t0, d;
    fm1_irq_off();
    t0 = real_us();
    master_pot();
    cr_ui_input();
    cr_ui_frame();                                /* the engine's snapshot, the LEDs */
    d = real_us() - t0;
    fm1_irq_on();
    if (d > emu_hal.ui_lock_max_us)
        emu_hal.ui_lock_max_us = d;
    cr_ui_draw();
    cr_settings_poll();                           /* main.c's settings_poll: a change saved once things are quiet */
}

void emu_fw_idle(void)                            /* main.c: the input scan while it waits for the next frame */
{
    fm1_irq_off();
    cr_ui_input();
    fm1_irq_on();
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
    printf("fm1_in: notes %07X buttons %04X  fm1_ms %u  pending edges %04X / notes %07X\n",
           (unsigned)fm1_in.notes, (unsigned)fm1_in.buttons, (unsigned)fm1_ms, (unsigned)host_pressed,
           (unsigned)host_notes);
    printf("  leds:");
    for (c = 0; c < FM1_NCOL; c++)
        printf(" %02X/%02X", fm1_led[c], fm1_led_dim[c]);
    printf("  (lit/dim per column)\n");
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
           "view %u layer %u options %u\n",
           (unsigned)cs.key_on, (unsigned)cs.tonic, (unsigned)cs.scale, (unsigned)cr.perform_on,
           (unsigned)cr.perform_mode, (unsigned)cr.bass_on, (unsigned)cs.bass_sound, (int)cr.voicing, (int)cu.octave,
           (unsigned)cs.bpm, (unsigned)cs.view, (unsigned)cu_layer(), (unsigned)cu.opt_open);
    printf("  parts busy %u  octave %d master_q12 %u cpu %u%%  midi in %u out %u\n", (unsigned)cr_parts_busy(),
           (int)cu.octave, (unsigned)song.master_q12, (unsigned)(song.cpu_q8 * 100u / 256u),
           (unsigned)(mi_w - mi_r), (unsigned)(mo_w - mo_r));
    printf("  loop: state %u cap %u layers %u events %u len %u slot %u played %u ring %u used %03X metro %u sig %u "
           "busy-loopv %d style %u single %u split %u\n",
           (unsigned)crl.state, (unsigned)crl.cap, (unsigned)crl.d.nlayers, (unsigned)crl.d.nev, (unsigned)crl.d.len,
           (unsigned)cs.loop_slot + 1u, (unsigned)crl.played, (unsigned)cr_loop_ring(&crl), (unsigned)cs.loop_used,
           (unsigned)crl.metro, (unsigned)crl.sig, cr_loop_busy(&cr, 0) + cr_loop_busy(&cr, 1) + cr_loop_busy(&cr, 2),
           (unsigned)cr.playstyle, (unsigned)cr.single, (unsigned)cr.split_pc);
    printf("  flash: %s, %u writes, settings saves %u (record: %s)\n", emu_flash_path[0] ? emu_flash_path : "RAM only",
           (unsigned)emu_flash_writes, (unsigned)crs_saves, crs_last_rc == 1 ? "current" : crs_last_rc == 2 ? "migrated" : "defaults");
    printf("  voices: given up %u (budget fades + overload sheds %u), own voices taken for a new note %u\n",
           (unsigned)voice_kills, (unsigned)shed_count, (unsigned)voice_steals);
    printf("  flash erases with the audio stalled %u (%u blocks of silence, %u ms each erase)\n", (unsigned)emu_stalls,
           (unsigned)emu_stall_blocks_all, (unsigned)EMU_ERASE_MS);
    fm1_irq_on();
}

/* the screen the last UI frame drew (emu.c EMU_UI_LOG): its kind, view, the texts that pick its glyphs, the animation
 * clock, the strips drawn and blitted */
void emu_fw_ui_info(char *buf, uint32_t n)
{
    static const char *const K[] = {"none", "stripes", "chord", "picker", "meter", "keyboard", "arp", "params", "geek",
                                    "text", "big", "scope", "edit8", "stack"};
    const cr_screen_t *s = &cu_scr;
    snprintf(buf, n, "%s view %u name '%s|%s|%s' from '%s|%s|%s' item '%s' value '%s' title '%s' size %u squeeze %u "
             "anim %02X %u ms ring %u/%u msg '%s' blits %u/%u bytes %u wv %u,%u,%u,%u,%u hot %u.%u",
             s->kind < CR_K_N && s->kind < sizeof K / sizeof K[0] ? K[s->kind] : "?", (unsigned)cs.view, s->name.root, s->name.quality, s->name.sup,
             s->from.root, s->from.quality, s->from.sup, s->kind == CR_K_PICKER ? cr_item(s, s->sel) : "",
             s->value, s->title, (unsigned)s->size, (unsigned)s->squeeze, (unsigned)s->anim,
             (unsigned)cr_anim_ms(&cu_anim, cu_now()), (unsigned)s->ring_on, (unsigned)s->ring, s->message,
             (unsigned)cr_dc.blits, (unsigned)cr_dc.drawn, (unsigned)cr_dc.bytes, (unsigned)s->wv[0], (unsigned)s->wv[1], (unsigned)s->wv[2], (unsigned)s->wv[3],
             (unsigned)s->wv[4], (unsigned)s->hot_r, (unsigned)s->hot_c);
}
void emu_fw_stats(uint32_t *shed, uint32_t *cpu_pct)
{
    *shed = shed_count;
    *cpu_pct = song.cpu_q8 * 100u / 256u;
}
