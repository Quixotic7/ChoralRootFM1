/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host tests of ChoralRoot's Bluetooth MIDI glue (firmware/src/cr_ble.c, HISTORY 3y236) with the real stack
 * (firmware/src/ble/) on the fake radio (tests/ble_fake/fm1_ble.h); the rest of the firmware stubbed:
 *   out     nothing queued unless a peer is ready and Bluetooth is On; clock and transport in their own ring, sent
 *           ahead of the notes; full rings refuse (the audio ISR never waits)
 *   in      MIDI only (SysEx and the rest dropped); into the input queue as source 3 with the packet's own time
 *   loss    the link going ends the notes the peer held (and only those) with note-offs, a full queue resumed at
 *           the next tick; the radio not started: the TIMER5 drain does nothing
 *   on/off  refused without the resource region or a calibration; On while not started waits for a quiet moment
 *           (nothing sounding, no loop); the record saved only when quiet; Off saved; the bond kept and forgotten
 *   boot    nothing read or started in SAFE MODE or without flash; On saved: started at power-on
 *   status  the Options line
 * Exit status: the number of failed checks. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define RING_PUBLISH() __asm__ volatile("" ::: "memory")
#define BLE_DMA_MEMORY __attribute__((aligned(4)))
#define FM1_RF_CAL_MEMORY
#define BLE_RF_RES

/* ---- what cr_ble.c takes from the rest of the firmware */
static volatile uint32_t fm1_ms = 5000;
static uint32_t ie_state = 1;
static uint32_t fm1_icfg(void) { return ie_state; }
static void fm1_irq_off(void) { ie_state = 0; }
static void fm1_icfg_set(uint32_t v) { ie_state = v; }
#define MQ 64u
static uint32_t midi_in_q[MQ], midi_in_src[MQ], midi_in_ms[MQ];
static volatile uint32_t mi_w, mi_r;
static int q_full;
static int midi_enqueue(uint32_t pkt, uint32_t source)
{
    if (q_full || mi_w - mi_r >= MQ)
        return 0;
    midi_in_q[mi_w % MQ] = pkt;
    midi_in_src[mi_w % MQ] = source;
    midi_in_ms[mi_w % MQ] = fm1_ms;
    mi_w++;
    return 1;
}
#define ST_PAYLOAD_MAX 4000u
#define OBJ_BLE 20u
static uint8_t st_obj[ST_PAYLOAD_MAX];
static int st_len = -1, st_saves;
static int st_load(uint32_t obj, void *dst, uint32_t max)
{
    if (obj != OBJ_BLE || st_len < 0 || (uint32_t)st_len > max)
        return -1;
    memcpy(dst, st_obj, (size_t)st_len);
    return st_len;
}
static int st_save(uint32_t obj, const void *src, uint32_t len)
{
    if (obj != OBJ_BLE || len > ST_PAYLOAD_MAX)
        return -1;
    memcpy(st_obj, src, len);
    st_len = (int)len;
    st_saves++;
    return 0;
}
static int st_read(uint32_t off, void *dst, uint32_t n) { (void)off; memset(dst, 0xFF, n); return 0; }  /* an empty VM */
static int flash_ok = 1, cr_safe, cr_res_ok = 1, parts_busy, crl_stage_busy;
static struct { uint32_t failed; } bootguard;
static int cr_parts_busy(void) { return parts_busy; }
enum { CRL_IDLE, CRL_PLAYING };
enum { CRL_CAP_NONE, CRL_CAP_ON };
static struct { int lstate, lcap; } cr_snap;
static void cu_cpy(char *d, const char *s, uint32_t n)
{
    uint32_t i;
    for (i = 0; s[i] && i + 1u < n; i++)
        d[i] = s[i];
    d[i] = 0;
}
static void cu_cat(char *d, const char *s, uint32_t n)
{
    uint32_t k = (uint32_t)strlen(d);
    if (k < n)
        cu_cpy(d + k, s, n - k);
}

#include "../firmware/src/cr_ble.h"
static void fm1_ble_crumb_boot(void) {}          /* (hal/fm1_ble.h: the start-up breadcrumbs; none here) */
#include "../firmware/src/cr_ble.c"

static int fails;
static void check(const char *what, int ok)
{
    printf("cr_ble: %-100s %s\n", what, ok ? "ok" : "FAIL");
    fails += !ok;
}
static uint32_t pk(uint32_t st, uint32_t a, uint32_t b) { return (st >> 4) | st << 8 | a << 16 | b << 24; }

static void fresh(void)                           /* a power-on, Bluetooth On saved, the radio started */
{
    memset(&crbl, 0, sizeof crbl);
    memset(&crbl_rec, 0, sizeof crbl_rec);
    mi_w = mi_r = 0;
    q_full = parts_busy = crl_stage_busy = cr_safe = 0;
    cr_snap.lstate = CRL_IDLE;
    cr_snap.lcap = CRL_CAP_NONE;
    flash_ok = cr_res_ok = 1;
    bootguard.failed = 0;
    st_len = -1;
    st_saves = 0;
}

static void t_out(void)
{
    uint32_t p, ms, i, n = 0;
    fresh();
    crbl.on = 1;
    check("out: nothing queued while no peer is ready", !cr_ble_emit(pk(0x90, 60, 100)) && crbl.tw == 0u);
    crbl.ready = 1;
    crbl.on = 0;
    check("out: nothing queued while Bluetooth is Off", !cr_ble_emit(pk(0x90, 60, 100)) && crbl.tw == 0u);
    crbl.on = 1;
    check("out: a note queued", cr_ble_emit(pk(0x90, 60, 100)) && crbl.tw == 1u);
    check("out: the clock in its own ring", cr_ble_emit(pk(0xF8, 0, 0)) && crbl.rtw == 1u && crbl.tw == 1u);
    check("out: Song Position in that ring too", cr_ble_emit(pk(0xF2, 16, 1)) && crbl.rtw == 2u);
    check("out: the clock sent ahead of the note queued first",
          ble_app_midi_peek(&p, &ms) && p == pk(0xF8, 0, 0) && ms == fm1_ms);
    ble_app_midi_pop();
    check("out: then Song Position", ble_app_midi_peek(&p, &ms) && p == pk(0xF2, 16, 1));
    ble_app_midi_pop();
    check("out: then the note", ble_app_midi_peek(&p, &ms) && p == pk(0x90, 60, 100));
    ble_app_midi_pop();
    check("out: then nothing", !ble_app_midi_peek(&p, &ms));
    for (i = 0; i < 2u * CRBL_Q; i++)
        n += (uint32_t)cr_ble_emit(pk(0x90, 60, 100));
    check("out: a full note ring refuses (the audio ISR never waits)", n == CRBL_Q);
    for (i = n = 0; i < 2u * CRBL_RT; i++)
        n += (uint32_t)cr_ble_emit(pk(0xF8, 0, 0));
    check("out: a full clock ring refuses", n == CRBL_RT);
    crbl.ready = 1;
    crbl.connected = 1;
    ble_app_state();                              /* (the stack: no link) */
    check("out: the peer gone: what was queued for it dropped, the loss flagged",
          crbl.tr == crbl.tw && crbl.rtr == crbl.rtw && crbl.lost && !crbl.ready);
}

static void t_in(void)
{
    fresh();
    crbl.on = crbl.started = 1;
    fm1_ms = 5000;
    ble_app_midi_in(pk(0x90, 60, 100), 100, 140); /* 40 ms before the packet's newest event */
    ble_app_midi_in(pk(0xF0, 0x7E, 0x7F), 0, 0);  /* SysEx: dropped */
    ble_app_midi_in(pk(0xFE, 0, 0), 0, 0);        /* active sensing: dropped */
    ble_app_midi_in(pk(0xFA, 0, 0), 0, 0);
    ble_app_midi_in(pk(0xB0, 74, 64), 0, 0);
    check("in: MIDI only (SysEx and active sensing dropped)", crbl.rw == 3u);
    cr_ble_drain();
    check("in: into the input queue as source 3, in order",
          mi_w == 3u && midi_in_q[0] == pk(0x90, 60, 100) && midi_in_src[0] == 3u && midi_in_q[1] == pk(0xFA, 0, 0) &&
          midi_in_q[2] == pk(0xB0, 74, 64) && midi_in_src[2] == 3u);
    check("in: stamped with the packet's own time (the peer's spacing kept)", midi_in_ms[0] == 4960u &&
          midi_in_ms[1] == 5000u);
    check("in: the peer's note held", (crbl.held[0][60 >> 4] >> (60 & 15)) & 1u);
    ble_app_midi_in(pk(0x80, 60, 0), 0, 0);
    cr_ble_drain();
    check("in: its note-off releases it", !((crbl.held[0][60 >> 4] >> (60 & 15)) & 1u));
    ble_app_midi_in(pk(0x93, 70, 90), 0, 0);
    ble_app_midi_in(pk(0x93, 70, 0), 0, 0);       /* a note-on at velocity 0 is a note-off */
    cr_ble_drain();
    check("in: a note-on at velocity 0 releases too", crbl.held[3][70 >> 4] == 0u);
    crbl.on = 0;
    ble_app_midi_in(pk(0x90, 61, 100), 0, 0);
    check("in: nothing taken while Off", crbl.rw == crbl.rr);
    crbl.on = 1;
    crbl.started = 0;
    ble_app_midi_in(pk(0x90, 61, 100), 0, 0);
    mi_w = mi_r = 0;
    cr_ble_drain();
    check("in: the radio not started: the TIMER5 drain does nothing", mi_w == 0u);
}

static void t_loss(void)
{
    uint32_t i, offs = 0;
    fresh();
    crbl.on = crbl.started = 1;
    ble_app_midi_in(pk(0x90, 60, 100), 0, 0);
    ble_app_midi_in(pk(0x95, 127, 100), 0, 0);
    ble_app_midi_in(pk(0x90, 0, 100), 0, 0);
    cr_ble_drain();
    mi_w = mi_r = 0;
    ble_app_midi_in(pk(0x90, 64, 100), 0, 0);     /* in the ring when the link goes: dropped, never sounds */
    crbl.lost = 1;
    q_full = 1;                                   /* the input queue full at first */
    cr_ble_drain();
    check("loss: a full queue: the note-offs wait (nothing dropped from held)",
          mi_w == 0u && crbl.releasing && crbl.rr == crbl.rw && crbl.held[0][0] && crbl.held[5][7]);
    q_full = 0;
    cr_ble_drain();
    for (i = 0; i < mi_w; i++)
        offs += midi_in_src[i] == 3u && ((midi_in_q[i] >> 8) & 0xF0u) == 0x80u;
    check("loss: then note-offs, source 3, for exactly the peer's three notes",
          mi_w == 3u && offs == 3u && midi_in_q[0] == (0x08u | 0x80u << 8 | 0u << 16) &&
          midi_in_q[1] == (0x08u | 0x80u << 8 | 60u << 16) && midi_in_q[2] == (0x08u | 0x85u << 8 | 127u << 16));
    check("loss: nothing held, nothing owed", !crbl.releasing && crbl.held[0][0] == 0u && crbl.held[0][3] == 0u &&
          crbl.held[5][7] == 0u);
    mi_w = mi_r = 0;
    cr_ble_drain();
    check("loss: done: the next tick adds nothing", mi_w == 0u);
    crbl.on = 1;
    ble_app_midi_in(pk(0x90, 62, 100), 0, 0);
    cr_ble_drain();
    mi_w = mi_r = 0;
    cr_ble_set(0);
    cr_ble_drain();
    check("loss: Off ends the peer's notes", mi_w == 1u && midi_in_q[0] == (0x08u | 0x80u << 8 | 62u << 16));
}

static void t_onoff(void)
{
    char s[40];
    fresh();
    crbl.booted = 1;
    cr_res_ok = 0;
    check("on: refused without the resource region", !cr_ble_set(1) && !crbl.on);
    cr_res_ok = 1;
    check("on: refused without a calibration", !cr_ble_set(1) && !crbl.on);
    crbl.cal = 1;
    parts_busy = 1;
    check("on: taken while notes sound; the radio's start waits", cr_ble_set(1) && crbl.on && crbl.pending &&
          !crbl.started && (crbl_rec.flags & CRBL_ON) && crbl.dirty);
    cr_ble_status(s, sizeof s);
    check("status: \"starts when nothing plays\"", !strcmp(s, "starts when nothing plays"));
    cr_ble_poll();
    check("poll: notes sounding: not started, nothing saved", !crbl.started && st_saves == 0);
    parts_busy = 0;
    cr_snap.lstate = CRL_PLAYING;
    cr_ble_poll();
    check("poll: a loop playing: not started, nothing saved", !crbl.started && st_saves == 0);
    cr_snap.lstate = CRL_IDLE;
    cr_snap.lcap = CRL_CAP_ON;
    cr_ble_poll();
    check("poll: a loop recording: not started", !crbl.started);
    cr_snap.lcap = CRL_CAP_NONE;
    crl_stage_busy = 1;
    cr_ble_poll();
    check("poll: a loop staged: not started", !crbl.started);
    crl_stage_busy = 0;
    cr_ble_poll();
    check("poll: quiet: started, a random static address made, saved", crbl.started && !crbl.pending &&
          crbl_rec.valid && (crbl_rec.addr[5] & 0xC0u) == 0xC0u && st_saves == 1 && !crbl.dirty &&
          ((crbl_rec_t *)st_obj)->magic == CRBL_MAGIC && (((crbl_rec_t *)st_obj)->flags & CRBL_ON));
    cr_ble_status(s, sizeof s);
    check("status: \"ChoralRoot XXXX: visible\"", !strncmp(s, "ChoralRoot ", 11) && strstr(s, ": visible") != 0);
    ble_app_bond((const uint8_t *)"RANDRAND", 0x1234u, (const uint8_t *)"0123456789abcdef");
    check("bond: kept (0xB5, 1, rand, ediv, ltk), saved at the next quiet poll",
          crbl_rec.bond[0] == 0xB5u && crbl_rec.bond[1] == 1u && !memcmp(crbl_rec.bond + 2, "RANDRAND", 8) &&
          ble_rd16(crbl_rec.bond + 10) == 0x1234u && !memcmp(crbl_rec.bond + 12, "0123456789abcdef", 16) &&
          crbl.dirty);
    cr_ble_poll();
    check("bond: saved", st_saves == 2 && ((crbl_rec_t *)st_obj)->bond[0] == 0xB5u);
    cr_ble_forget();
    cr_ble_poll();
    check("forget: the bond gone, saved", crbl_rec.bond[0] == 0u && st_saves == 3 &&
          ((crbl_rec_t *)st_obj)->bond[0] == 0u);
    check("off: taken", cr_ble_set(0) && !crbl.on && !(crbl_rec.flags & CRBL_ON));
    cr_ble_status(s, sizeof s);
    check("status: \"MIDI over Bluetooth: off\"", !strcmp(s, "MIDI over Bluetooth: off"));
    cr_ble_poll();
    check("off: saved", st_saves == 4 && !(((crbl_rec_t *)st_obj)->flags & CRBL_ON));
    cr_res_ok = 0;
    cr_ble_status(s, sizeof s);
    check("status: no region: \"install ChoralRoot again\"", !strcmp(s, "install ChoralRoot again"));
}

static void t_boot(void)
{
    crbl_rec_t r;
    fresh();
    memset(&r, 0, sizeof r);
    r.magic = CRBL_MAGIC;
    r.ver = 1;
    r.flags = CRBL_ON;
    r.valid = 1;
    memcpy(r.addr, "\x01\x02\x03\x04\x05\xC6", 6);
    memcpy(st_obj, &r, sizeof r);
    st_len = (int)sizeof r;
    cr_safe = 1;
    cr_ble_boot();
    check("boot: SAFE MODE: nothing read, nothing started", !crbl.booted && !crbl.started && !crbl.on);
    cr_safe = 0;
    cr_ble_boot();
    check("boot: On saved: started at power-on with the saved address (no calibration stored: measured at start)",
          crbl.booted && crbl.on && crbl.started && crbl.cal && fm1_rf_cal_on && !memcmp(crbl_rec.addr, r.addr, 6));
    fresh();
    r.flags = 0;
    memcpy(st_obj, &r, sizeof r);
    st_len = (int)sizeof r;
    cr_ble_boot();
    check("boot: Off saved (the default): not started", crbl.booted && !crbl.on && !crbl.started);
    fresh();
    cr_ble_boot();
    check("boot: no record: Off, not started", crbl.booted && !crbl.on && !crbl.started && crbl_rec.magic == 0u);
    fresh();
    r.flags = CRBL_ON;
    memcpy(st_obj, &r, sizeof r);
    st_len = (int)sizeof r;
    cr_res_ok = 0;
    cr_ble_boot();
    check("boot: On saved but the region missing: not started", crbl.on && !crbl.started);
}

int main(void)
{
    t_out();
    t_in();
    t_loss();
    t_onoff();
    t_boot();
    printf("cr_ble: %s (%d failed)\n", fails ? "FAILED" : "all passed", fails);
    return fails;
}
