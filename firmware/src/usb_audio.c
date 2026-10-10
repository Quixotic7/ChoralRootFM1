/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * ChoralRoot changes (the PCM16-only capture, SAFE MODE, the console's presentation, the playback function removed)
 * Copyright (C) 2026 ChoralRoot FM-1 contributors */
/* Melodee's usb_audio.c (Melodee 0.11.1). USB0 full-speed isochronous service, included by usb.c after the SIE
 * helpers.
 * The AC79 usb_phy.h defines bit 14 as ISOCHRONOUS in TX/RX CSR. As in the
 * SDK and the MIDI endpoints, MaxP is written as 0xFF and TX bit 13 (direction)
 * stays clear: an exact MaxP turns on double packet buffering, and then every
 * other OUT packet never reaches its buffer (Melodee's playback; ChoralRoot
 * has the recording only). All SIE access stays in TIMER5, including
 * SET_INTERFACE and reset handling; register access goes through
 * hal/fm1_usb.h. */
#define UA_IF_CAP 3u                            /* the streaming interface in CFG_DESC (usb_audio_desc.h) */
#define UA_OFF_IN 1u                            /* ua_off: the recording left out of the configuration */
#define UA_NO_IF 0xFFu
#ifndef UAC_BLOCKED
#define UAC_BLOCKED() 0                         /* (core.h: SAFE MODE, no audio function, the console instead) */
#endif
static uint8_t ua_off;                          /* what the host is given (ua_cfg_build at usb_start); UA_OFF_IN with
                                                 * FELUCCA_CDC: the console is presented instead (usb.c usb_cdc_on) */
static uint8_t ua_off_want;                     /* Options > USB Record (ua_off_set; cr_settings.c at boot);
                                                 * ua_off_apply makes it ua_off */
static uint32_t ua_off_ms;                      /* its last change */
static uint8_t ua_if_cap = UA_IF_CAP, ua_nif = 4;   /* as sent (ua_cfg_build) */
#define UA_DMA_PACKET ((UA_PACKET + 3u) & ~3u)
static uint8_t ua_tx[2][UA_DMA_PACKET] __attribute__((aligned(4)));
static uint32_t ua_tx_bytes;
static uint8_t ua_tx_slot;
static uint16_t ua_frame;
static uint8_t ua_frame_valid, ua_paused;
static uint8_t ua_rate_pending, ua_rate_reply[3];
static uint8_t ua_armed;                        /* a packet has been armed since the stream opened */
/* 1 while a context is in an SIE register sequence (INDEX, then the indexed registers; CON1's start / done): TIMER5's
 * USB work (main.c) and the audio ISR's endpoint service (audio.c ua_isr_service) never enter one while another is
 * under way, whatever preempts what (main.c) */
static volatile uint8_t usb_sie_owner;
#ifdef FM1_TICKS_PER_US
#define UA_DBG_NOW() fm1_ticks()
#define UA_DBG_US FM1_TICKS_PER_US
#else
#define UA_DBG_NOW() 0u                                 /* (the host tests: no clock) */
#define UA_DBG_US 1u
#endif
/* the endpoint's diagnostics (cr_backup.c's DEBUG command, tools/fm1_install.py --debug): who serviced it, how often
 * the packet slot was found free or still armed, the TXCSR1 bits seen, the frames */
static struct {
    uint32_t svc_t5, svc_isr, isr_calls, isr_gated;   /* services from TIMER5 / the audio ISR; ISR calls; gated out */
    uint32_t fill_free, fill_busy, csr_or, csr_last;  /* ua_tx_fill: slot free (a packet armed) / still armed; bits */
    uint32_t sofs, frame_last, armed_frame_dup;       /* frames seen; the last; two packets armed in one frame */
    uint32_t armed_frame;
    uint32_t arm_last, arm_gap_max, arm_gap[3];       /* arm to arm (TIMER4 ticks): the longest; < 1.2, < 2.5, more ms */
    uint32_t open_at, open_ms, opens;                 /* the stream: opened at (fm1_ms), ms open in all, times opened */
    uint32_t arm_calls;                               /* ungated arm checks (main.c ua_service_paced) */
    uint32_t take_bin[6], first_take_ms, first_take_max, open_close_ms, holds;   /* the host's takes after an
                                                 * open (50 ms bins); its first take; open to close; reads stopped */
    uint32_t pk528, pk540, pk_other, pk_other_last;   /* packets armed by size (44, 45 frames; anything else) */
    uint32_t frames_open, frames_last_open;           /* frames sent in this open / the last whole one */
    uint32_t isr_busy, t5_busy;                       /* skipped: the SIE was in another context's sequence */
    uint32_t stalls, log_n, ovr_seen;                 /* EP0 requests stalled; events logged; overruns logged */
    uint32_t log[16][2];                              /* the last 16 events: fm1_ms, what (UA_EV_*) */
} ua_dbg;
/* an event: a SETUP (bmRequestType | bRequest << 8 | wValue low << 16 | wIndex low << 24, the stalled ones repeated
 * with bit 31 of the time set) or an overrun (0xFF0000nn: the count) */
static void ua_log(uint32_t ms, uint32_t what)
{
    uint32_t k = ua_dbg.log_n++ & 15u;
    ua_dbg.log[k][0] = ms;
    ua_dbg.log[k][1] = what;
}

/* CFG_DESC without the recording when ua_off has it (MIDI only: a build without the console). The AC header's
 * streaming interface list is kept as it is (the function goes whole); an absent stream gets UA_NO_IF. */
static void ua_cfg_build(void)
{
    uint32_t i, k, n = 0, nif = 0, skip = 0;
    ua_if_cap = UA_NO_IF;
#if FELUCCA_CDC
    if (usb_cdc_on) {                           /* the console's configuration (CFG_DESC_CDC): MIDI and CDC */
        ua_nif = 4;
        return;
    }
#endif
    for (i = 0; i < sizeof CFG_DESC; i += CFG_DESC[i]) {
        const uint8_t *d = CFG_DESC + i;
        if (d[1] == 0x0Bu)                      /* IAD: a function starts */
            skip = (ua_off & UA_OFF_IN) && d[2] + 1u == UA_IF_CAP;
        if (skip)
            continue;
        for (k = 0; k < d[0]; k++)
            ua_cfg[n + k] = d[k];
        n += d[0];
        if (d[1] == 4u) {
            nif += d[3] == 0u;
            if (d[2] == UA_IF_CAP)
                ua_if_cap = d[2];
        }
    }
    ua_cfg[2] = (uint8_t)n;
    ua_cfg[3] = (uint8_t)(n >> 8);
    ua_cfg[4] = (uint8_t)nif;
    ua_cfg_len = (uint16_t)n;
    ua_nif = (uint8_t)nif;
}

static void ua_cap_config(void)
{
    ua_tx_bytes = ua_tx_slot = 0;
    fm1_usb_ep_txbuf(2, ua_tx[0]);
    sie_wr(S_INDEX, 2);
    sie_wr(S_TXMAXP, 0xFF);
    sie_wr(S_TXCSR1, 0x48);
    sie_wr(S_TXCSR2, 0x40);
    fm1_usb_ep_enable(1u << 2);
}

static void ua_hw_stop(void)
{
    ua_reset();
    ua_armed = 0;
    ua_rate_pending = 0;
    ua_frame_valid = ua_paused = 0;
    ua_tx_bytes = ua_tx_slot = 0;
    sie_wr(S_INDEX, 2);
    sie_wr(S_TXCSR1, 0x48);
}

/* Keep one packet queued on the IN endpoint. Prepare capture's next packet
 * while DMA owns the current one, keeping PCM packing out of the critical
 * path between noticing completion and arming the next IN transfer.
 * An empty IN response loses a millisecond of the host's audio clock. */
static void ua_tx_fill(void)
{
    if (ua.cap_alt) {
        uint32_t csr;
        sie_wr(S_INDEX, 2);
        csr = sie_rd(S_TXCSR1);
        ua_dbg.csr_or |= csr;
        ua_dbg.csr_last = csr;
        if (csr & 1u) {
            ua_dbg.fill_busy++;
            /* the host stopped reading (a packet waiting 20 ms) without closing: the ring stops being fed */
            if (ua_armed && !ua.cap_hold && ua_dbg.arm_last && UA_DBG_NOW() - ua_dbg.arm_last > 20000u * UA_DBG_US) {
                ua.cap_hold = 1;
                ua_cap_reset();
                ua_dbg.holds++;
            }
        }
        if (!(csr & 1u)) {
            if (ua_armed) {                     /* the host took the armed packet */
                uint32_t since = fm1_ms - ua_dbg.open_at;
                if (since < 300u)
                    ua_dbg.take_bin[since / 50u]++;
                if (ua.cap_hold) {              /* its first (or first again): the ring is fed from now, primes, */
                    ua.cap_hold = 0;            /* then plays: no overrun while the host has not started */
                    ua_cap_reset();
                    if (since > ua_dbg.first_take_max)
                        ua_dbg.first_take_max = since;
                    ua_dbg.first_take_ms = since;
                }
            }
            ua_armed = 1;
            ua_dbg.fill_free++;
            if (ua_frame_valid && ua_dbg.armed_frame == ua_frame)
                ua_dbg.armed_frame_dup++;
            ua_dbg.armed_frame = ua_frame;
            {
                uint32_t now = UA_DBG_NOW(), g = now - ua_dbg.arm_last;
                if (ua_dbg.arm_last) {
                    if (g > ua_dbg.arm_gap_max)
                        ua_dbg.arm_gap_max = g;
                    ua_dbg.arm_gap[g < 1200u * UA_DBG_US ? 0 : g < 2500u * UA_DBG_US ? 1 : 2]++;
                }
                ua_dbg.arm_last = now;
            }
            if (!ua_tx_bytes)                   /* first packet after a stream reset */
                ua_tx_bytes = ua_transmit(ua_tx[ua_tx_slot]);
            if (ua_tx_bytes == 528u)
                ua_dbg.pk528++;
            else if (ua_tx_bytes == 540u)
                ua_dbg.pk540++;
            else {
                ua_dbg.pk_other++;
                ua_dbg.pk_other_last = ua_tx_bytes;
            }
            ua_dbg.frames_open += ua_tx_bytes / (2u * UA_CAP_CHANNELS);
            fm1_usb_ep_send(2, ua_tx[ua_tx_slot], ua_tx_bytes);
            sie_wr(S_TXCSR1, 1);
            ua.tx_packets++;
            ua_tx_slot ^= 1u;
            ua_tx_bytes = 0;
        }
    }
}

static void ua_tx_prepare(void)
{
    if (ua.cap_alt && !ua_tx_bytes)
        ua_tx_bytes = ua_transmit(ua_tx[ua_tx_slot]);
}

static int ua_set_interface(uint16_t interface, uint16_t alt)
{
    if (!usb.config || interface == UA_NO_IF || interface != ua_if_cap || alt > 1u)   /* capture PCM16 */
        return 0;
    if (UAC_BLOCKED())
        alt = 0;                                /* (SAFE MODE presents no audio function: belt and braces) */
    ua_cap_reset();
    if (alt && !ua.cap_alt) {
        ua_dbg.open_at = fm1_ms;
        ua_dbg.opens++;
        ua_dbg.arm_last = 0;
        ua_dbg.frames_open = 0;
    } else if (!alt && ua.cap_alt) {
        ua_dbg.frames_last_open = ua_dbg.frames_open;
        ua_dbg.open_ms += fm1_ms - ua_dbg.open_at;
        ua_dbg.open_close_ms = fm1_ms - ua_dbg.open_at;
    }
    ua.cap_alt = (uint8_t)alt;
    ua.cap_hold = (uint8_t)alt;                 /* fed once the host takes its first packet (ua_tx_fill) */
    ua_armed = 0;
    if (alt)
        ua_cap_config();
    else {
        sie_wr(S_INDEX, 2);
        sie_wr(S_TXCSR1, 0x48);
        ua_tx_bytes = ua_tx_slot = 0;
    }
    if (alt) {
        ua_tx_fill();                           /* ready for the host's first IN token */
        ua_tx_prepare();
    }
    return 1;
}

/* UAC1 endpoint sampling-frequency control. There is only one discrete rate;
 * retain these requests for hosts that set it while starting a stream.
 * The capture's one alternate is PCM16. */
static int ua_control_setup(const uint8_t *s)
{
    uint32_t rate;
    if (!usb.config || s[2] || s[3] != 1u || s[5] || s[6] != 3u || s[7] || s[4] != 0x82u || ua_if_cap == UA_NO_IF)
        return 0;                            /* (not the capture endpoint, or the recording left out) */
    if (s[0] == 0x22u && s[1] == 1u) {       /* SET_CUR: three-byte OUT stage */
        ua_rate_pending = s[4];
        sie_wr(S_INDEX, 0);
        sie_wr(S_CSR0, 0x40);
        return 1;
    }
    if (s[0] != 0xA2u)
        return 0;
    switch (s[1]) {
    case 0x81:                               /* GET_CUR */
    case 0x82:                               /* GET_MIN */
    case 0x83: rate = UA_RATE; break;          /* GET_MAX */
    case 0x84: rate = 0; break;                /* GET_RES: fixed frequency */
    default: return 0;
    }
    ua_rate_reply[0] = (uint8_t)rate;
    ua_rate_reply[1] = (uint8_t)(rate >> 8);
    ua_rate_reply[2] = (uint8_t)(rate >> 16);
    e0_send(ua_rate_reply, 3, 3);
    return 1;
}

static int ua_control_data(const uint8_t *p, uint32_t n)
{
    uint8_t ep = ua_rate_pending;
    (void)p;
    ua_rate_pending = 0;
    if (!usb.config || !ep || n != 3u)
        return 0;
    /* UAC1 rounds unsupported values to the closest discrete rate: 44100.
     * Do not flush an already running stream for this no-op control. */
    return 1;
}

static void ua_hw_poll(void)
{
    uint32_t n, f;
    if (!usb.config)
        return;
    if (usb.suspended) {
        if (!ua_paused) {
            ua_cap_reset();
            if (ua.cap_alt)
                ua_cap_config();
            ua_paused = 1;
            ua_frame_valid = 0;
        }
        return;
    }
    if (ua_paused) {
        if (ua.cap_alt)
            ua_cap_config();
        ua_paused = 0;
    }
    if (!ua.cap_alt) {
        ua_frame_valid = 0;                     /* idle frames are not missed ones */
        return;
    }
    ua_tx_fill();
    /* FRAME1 may roll over between reads: retry with a stable high byte. */
    do {
        n = sie_rd(S_FRAME2) & 7u;
        f = sie_rd(S_FRAME1);
    } while (n != (sie_rd(S_FRAME2) & 7u) && usb.up);
    f |= n << 8;
    if (!ua_frame_valid || f != ua_frame) {
        if (ua_frame_valid) {
            uint32_t gap = (f - ua_frame) & 2047u;
            if (gap > 1u)
                ua.missed_frames += gap - 1u;
        }
        ua_frame = (uint16_t)f;
        ua_frame_valid = 1;
        ua_dbg.sofs++;
        ua_dbg.frame_last = f;
        ua_sof();
    }
    ua_tx_fill();
    ua_tx_prepare();
}
