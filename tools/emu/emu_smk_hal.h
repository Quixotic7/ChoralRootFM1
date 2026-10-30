/* SPDX-License-Identifier: GPL-3.0-only
 * SMK-37 port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro) */
/* The SMK-37's input on the emulator (EMU_SMK37: tools/emu/web/build_web.sh's SMK module; emu_hal_fw.h includes this
 * instead of its FM-1 keys and LEDs). The firmware's own SMK input layer, hal/smk_input.h, does the work: its frame
 * logic (the keys' two contacts and their velocity through the factory curves, the buttons' debounce, the encoders'
 * quadrature decoder, the button LEDs) and its analog logic (the pads' hit, peak and release, the faders' filter, the
 * wheels, the battery) run unchanged on a modelled panel: emu_smk_tick builds the matrix the scan would have read
 * (fm1_in.raw: 16 columns of 8 rows) every 1.6 ms and the 24 analog slots' readings every 100 us, from what the page
 * holds (emu_smk). Only the scan's register side (the SPI2 shift, the latch, the ADC) is left out (SMK_INPUT_HOST).
 *   a key held at velocity v: contact 1 closes at once, contact 2 after the time the factory curve reads as v
 *   a button: closed while held (at least 8 ms: the debounce sees a short click)
 *   a knob: one detent = four quadrature states, each held two frames (the decoder's rule), + = clockwise
 *   a pad hit at velocity v: the reading the pad's curve reads as v while held, the baseline (100) otherwise
 *   a fader 0..127, a wheel: the raw reading; the pedal unplugged (985), the battery full (620)
 * The pads' colours (lcd.c padled_set on the unit) land in emu_smk.pad_rgb. */
#define SMK_INPUT_HOST 1
#define FM1_INPUT_IDLE() ((void)0)
static inline uint32_t fm1__lock(void) { fm1_irq_off(); return 0; }
static inline void fm1__unlock(uint32_t v) { (void)v; fm1_irq_on(); }
#include "../../firmware/hal/fm1_flash_map.h"         /* (storage.c's SMK windows) */
#include "../../firmware/hal/smk_input.h"
#define KN_QUICK() (emu_hal.knob_quick)
static int emu_pump_begin(void)
{
    if (emu_hal.pump_off)
        return 0;
    if (emu_hal.pump_mark)
        emu_hal.pump_mark();
    fm1_irq_off();
    return 1;
}
#define CU_PUMP_BEGIN() emu_pump_begin()
#define CU_PUMP_END() fm1_irq_on()
#define CR_HOOK_STRIP() do { if (emu_hal.strip_done) emu_hal.strip_done(); } while (0)

/* lcd.c's pad LEDs (SMK): the colours the firmware sets, GRB as sent; the console's pad test hold */
static uint32_t padled_hold_until;
#define EMU_SMK_NPAD 16u
static struct {
    uint8_t key[FM1_NKEY], key_vel[FM1_NKEY];          /* held; its velocity (1..127) */
    uint32_t key_t[FM1_NKEY], key_up[FM1_NKEY];        /* us it went down; a release asked for (deferred) */
    uint8_t btn[FM1_NBTN], btn_up[FM1_NBTN];
    uint32_t btn_t[FM1_NBTN];
    int32_t enc_want[FM1_NENC];                        /* detents still to turn (x 4 quadrature states) */
    uint8_t enc_phase[FM1_NENC], enc_hold[FM1_NENC];   /* 0..3 along 0 -> 2 -> 3 -> 1 (+), frames held */
    uint16_t pad_raw[EMU_SMK_NPAD];                    /* the pad's reading (100: at rest) */
    uint16_t fader_raw[4], wheel_raw[2];
    uint16_t pedal_raw;                                /* the pedal jack (985: nothing plugged in, a pull-up) */
    uint32_t pad_rgb[EMU_SMK_NPAD];                    /* 0xRRGGBB as padled_set got it */
    uint32_t us, frame_us, slot_us;                    /* the model's clock: the next frame, the next slot */
    uint8_t slot, ready;
} emu_smk;
static const uint8_t EMU_SMK_QUAD[4] = {0, 2, 3, 1};    /* the states along a clockwise turn (decoder: B leads) */
static void padled_set(uint32_t pad, uint8_t r, uint8_t g, uint8_t b)
{
    if (pad < EMU_SMK_NPAD)
        emu_smk.pad_rgb[pad] = (uint32_t)r << 16 | (uint32_t)g << 8 | b;
}

static void emu_smk_init(void)                     /* fm1_input_init's RAM side (its pins and SPI are not modelled) */
{
    uint32_t i;
    smk_trim_defaults();
    for (i = 0; i < FM1_NENC; i++) {
        fm1_in.enc_last[i] = 0xFF;
        emu_smk.enc_phase[i] = 1;                      /* (one state into a turn: each detent then gives one step) */
    }
    for (i = 0; i < SMK_NPAD; i++) {
        fm1_in.pad_base[i] = 100u;
        emu_smk.pad_raw[i] = 100u;
    }
    emu_smk.wheel_raw[0] = 535;                        /* (the guide's unit: the pitch wheel at rest) */
    emu_smk.wheel_raw[1] = 629;                        /* the mod wheel at the bottom (up = lower readings) */
    for (i = 0; i < 2u; i++)
        fm1_in.wheel[i] = emu_smk.wheel_raw[i];
    emu_smk.pedal_raw = 985;                           /* the guide's unit with the jack empty */
    fm1_in.pedal = emu_smk.pedal_raw;
    emu_smk.ready = 1;
}

/* the contact-2 time (us) the stock algorithm and key k's curve read as velocity v (the quickest such stroke) */
static uint32_t emu_smk_dt(uint32_t k, uint32_t v)
{
    uint32_t T;
    for (T = 10; T < 1022u; T++)
        if (smk_vel7(smk_curve(smk_trim_key[k], 0x3FFu - T)) <= v)
            return (T - 10u) * 100u;
    return 1012u * 100u;
}
/* the reading pad p's curve reads as velocity v */
static uint16_t emu_smk_pad_for(uint32_t p, uint32_t v)
{
    uint32_t r;
    for (r = fm1_in.pad_base[p] + SMK_PAD_ON + 1u; r < 1023u; r++)
        if (smk_vel7(smk_curve(smk_trim_pad[p], r)) >= v)
            return (uint16_t)r;
    return 1023u;
}

static uint32_t emu_smk_closed(void)               /* the matrix the scan would read now -> fm1_in.raw */
{
    uint8_t raw[FM1_NCOL] = {0};
    uint32_t i, now = emu_smk.us;
#define CLOSE(code) (raw[(code) >> 4] |= (uint8_t)(1u << ((code) & 15u)))
    for (i = 0; i < FM1_NKEY; i++) {
        if (!emu_smk.key[i])
            continue;
        CLOSE(SMK_KEYC[i][0]);
        if (now - emu_smk.key_t[i] >= emu_smk_dt(i, emu_smk.key_vel[i]))
            CLOSE(SMK_KEYC[i][1]);
    }
    for (i = 0; i < FM1_NBTN; i++)
        if (emu_smk.btn[i])
            CLOSE(SMK_BTNC[i]);
    for (i = 0; i < FM1_NENC; i++) {
        uint32_t st = EMU_SMK_QUAD[emu_smk.enc_phase[i] & 3u];
        if (st & 1u)
            CLOSE(SMK_ENCC[i][0]);
        if (st & 2u)
            CLOSE(SMK_ENCC[i][1]);
    }
#undef CLOSE
    for (i = 0; i < FM1_NCOL; i++)
        fm1_in.raw[i] = raw[i];
    return 0;
}

static void emu_smk_frame(void)                    /* every 1.6 ms: the releases due, the knobs, then smk_input's frame */
{
    uint32_t i, now = emu_smk.us;
    for (i = 0; i < FM1_NKEY; i++)                     /* a release once contact 2 has been seen (4 ms after it) */
        if (emu_smk.key[i] && emu_smk.key_up[i] && now - emu_smk.key_t[i] >= emu_smk_dt(i, emu_smk.key_vel[i]) + 4000u)
            emu_smk.key[i] = emu_smk.key_up[i] = 0;
    for (i = 0; i < FM1_NBTN; i++)
        if (emu_smk.btn[i] && emu_smk.btn_up[i] && now - emu_smk.btn_t[i] >= 8000u)
            emu_smk.btn[i] = emu_smk.btn_up[i] = 0;
    for (i = 0; i < FM1_NENC; i++) {
        if (!emu_smk.enc_want[i] || ++emu_smk.enc_hold[i] < 2u)
            continue;
        emu_smk.enc_hold[i] = 0;
        if (emu_smk.enc_want[i] > 0) {
            emu_smk.enc_phase[i] = (uint8_t)((emu_smk.enc_phase[i] + 1u) & 3u);
            emu_smk.enc_want[i]--;
        } else {
            emu_smk.enc_phase[i] = (uint8_t)((emu_smk.enc_phase[i] + 3u) & 3u);
            emu_smk.enc_want[i]++;
        }
    }
    emu_smk_closed();
    fm1__frame();
}
static uint32_t emu_smk_slot_raw(uint32_t slot)    /* analog slot s: mux address s / 3, channel A (pads 0..7), B (8..15), C */
{
    uint32_t a = slot / 3u, ch = slot % 3u;
    if (ch == 0u)
        return emu_smk.pad_raw[SMK_PADMUX[a]];
    if (ch == 1u)
        return emu_smk.pad_raw[SMK_PADMUX[a] + 8u];
    if (a < 4u)
        return emu_smk.fader_raw[SMK_FADMUX[a]];
    if (a < 6u)
        return emu_smk.wheel_raw[a - 4u];
    return a == 6u ? emu_smk.pedal_raw : 620u;
}
/* the 1 ms tick: ten 100 us scan ticks (one analog slot each), a frame every 16 */
static void emu_smk_tick(uint32_t ms)
{
    uint32_t t;
    if (!emu_smk.ready)
        return;
    for (t = 0; t < 10u; t++) {
        emu_smk.us = ms * 1000u + t * 100u;
        if (emu_sim)
            emu_sim_us = emu_smk.us;
        smk__analog(emu_smk.slot, emu_smk_slot_raw(emu_smk.slot));
        emu_smk.slot = (uint8_t)(emu_smk.slot + 1u == 24u ? 0u : emu_smk.slot + 1u);
        if ((int32_t)(emu_smk.us - emu_smk.frame_us) >= 0) {
            emu_smk.frame_us = emu_smk.us + 1600u;
            emu_smk_frame();
        }
    }
    if (emu_sim)
        emu_sim_us = ms * 1000u;
}

/* the page's panel (emu_web.c's web_smk_* exports) */
static void emu_smk_key(uint32_t k, uint32_t vel)
{
    if (k >= FM1_NKEY)
        return;
    if (vel) {
        emu_smk.key[k] = 1;
        emu_smk.key_up[k] = 0;
        emu_smk.key_vel[k] = (uint8_t)(vel > 127u ? 127u : vel);
        emu_smk.key_t[k] = emu_smk.us;
    } else if (emu_smk.key[k]) {
        emu_smk.key_up[k] = 1;
    }
}
static void emu_smk_button(uint32_t b, int down)
{
    if (b >= FM1_NBTN)
        return;
    if (down) {
        if (!emu_smk.btn[b])
            emu_smk.btn_t[b] = emu_smk.us;
        emu_smk.btn[b] = 1;
        emu_smk.btn_up[b] = 0;
    } else if (emu_smk.btn[b]) {
        emu_smk.btn_up[b] = 1;
    }
}
static void emu_smk_pad(uint32_t p, uint32_t vel)
{
    if (p < EMU_SMK_NPAD)
        emu_smk.pad_raw[p] = vel ? emu_smk_pad_for(p, vel > 127u ? 127u : vel) : fm1_in.pad_base[p];
}
