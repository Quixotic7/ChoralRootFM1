/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * ChoralRoot changes (the PCM16-only capture, SAFE MODE, the console's presentation) Copyright (C) 2026 ChoralRoot
 * FM-1 contributors */
/* Melodee's usb_audio.c (Melodee 0.11.1). USB0 full-speed isochronous service, included by usb.c after the SIE
 * helpers.
 * The AC79 usb_phy.h defines bit 14 as ISOCHRONOUS in TX/RX CSR. As in the
 * SDK and the MIDI endpoints, MaxP is written as 0xFF and TX bit 13 (direction)
 * stays clear: an exact MaxP turns on double packet buffering, and then every
 * other OUT packet never reaches ua_rx. RX DMA keeps the same four-byte guard
 * as USB MIDI. All SIE access stays in TIMER5, including SET_INTERFACE and
 * reset handling; register access goes through hal/fm1_usb.h. */
#define UA_IF_PLAY 3u                           /* streaming interfaces in CFG_DESC (usb_audio_desc.h) */
#define UA_IF_CAP 5u
#define UA_OFF_OUT 1u                           /* ua_off: functions left out of the configuration */
#define UA_OFF_IN 2u
#define UA_NO_IF 0xFFu
#ifndef UAC_BLOCKED
#define UAC_BLOCKED() 0                         /* (core.h: SAFE MODE, no audio function, the console instead) */
#endif
static uint8_t ua_off;                          /* what the host is given (ua_cfg_build at usb_start); both bits with
                                                 * FELUCCA_CDC: the console is presented instead (usb.c usb_cdc_on) */
static uint8_t ua_off_want;                     /* Options > USB Audio Out / In (ua_off_set; cr_settings.c at boot);
                                                 * ua_off_apply makes it ua_off */
static uint32_t ua_off_ms;                      /* its last change */
static uint8_t ua_if_play = UA_IF_PLAY, ua_if_cap = UA_IF_CAP, ua_nif = 6;   /* as sent (ua_cfg_build) */
#define UA_DMA_PACKET ((UA_PACKET + 3u) & ~3u)
static uint8_t ua_tx[2][UA_DMA_PACKET] __attribute__((aligned(4)));
static uint32_t ua_tx_bytes;
static uint8_t ua_tx_slot;
static uint8_t ua_rx[((UA_PLAY_PACKET + 3u) & ~3u) + 4u] __attribute__((aligned(4)));
static uint8_t ua_fb[4] __attribute__((aligned(4)));
static uint16_t ua_frame;
static uint8_t ua_frame_valid, ua_paused;
static uint8_t ua_rate_pending, ua_rate_reply[3];

/* CFG_DESC without the functions in ua_off. Interfaces after a missing one
 * move down to stay contiguous: IADs, interface descriptors and the AC
 * header's streaming interface list. Absent streams get UA_NO_IF. */
static void ua_cfg_build(void)
{
    uint32_t i, k, n = 0, nif = 0, shift = 0, skip = 0, ac = 0;
    ua_if_play = ua_if_cap = UA_NO_IF;
#if FELUCCA_CDC
    if (usb_cdc_on) {                           /* the console's configuration (CFG_DESC_CDC): MIDI and CDC */
        ua_nif = 4;
        return;
    }
#endif
    for (i = 0; i < sizeof CFG_DESC; i += CFG_DESC[i]) {
        const uint8_t *d = CFG_DESC + i;
        uint8_t *o = ua_cfg + n;
        if (d[1] == 0x0Bu) {                    /* IAD: a function starts */
            skip = ua_off & (d[2] + 1u == UA_IF_PLAY ? UA_OFF_OUT : d[2] + 1u == UA_IF_CAP ? UA_OFF_IN : 0u);
            if (skip)
                shift += d[3];
        }
        if (skip)
            continue;
        for (k = 0; k < d[0]; k++)
            o[k] = d[k];
        n += d[0];
        if (d[1] == 0x0Bu) {
            o[2] = (uint8_t)(d[2] - shift);
        } else if (d[1] == 4u) {
            o[2] = (uint8_t)(d[2] - shift);
            ac = d[5] == 1u && d[6] == 1u;      /* audio control: its header lists the streams */
            nif += d[3] == 0u;
            if (d[2] == UA_IF_PLAY)
                ua_if_play = o[2];
            else if (d[2] == UA_IF_CAP)
                ua_if_cap = o[2];
        } else if (ac && d[1] == 0x24u && d[2] == 1u) {
            for (k = 0; k < d[7]; k++)
                o[8 + k] = (uint8_t)(d[8 + k] - shift);
        }
    }
    ua_cfg[2] = (uint8_t)n;
    ua_cfg[3] = (uint8_t)(n >> 8);
    ua_cfg[4] = (uint8_t)nif;
    ua_cfg_len = (uint16_t)n;
    ua_nif = (uint8_t)nif;
}

static void ua_play_config(void)
{
    fm1_usb_ep_rxbuf(2, ua_rx);
    sie_wr(S_INDEX, 2);
    sie_wr(S_RXMAXP, 0xFF);
    sie_wr(S_RXCSR1, 0x90);                     /* flush FIFO, clear toggle */
    sie_wr(S_RXCSR2, 0x40);                     /* ISOCHRONOUS */
    fm1_usb_ep_txbuf(3, ua_fb);
    sie_wr(S_INDEX, 3);
    sie_wr(S_TXMAXP, 0xFF);
    sie_wr(S_TXCSR1, 0x48);
    sie_wr(S_TXCSR2, 0x40);                     /* ISOCHRONOUS */
    sie_wr(S_INTRRX1E, sie_rd(S_INTRRX1E) | 0x04u);
    fm1_usb_ep_enable((1u << 2) | (1u << 3));
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
    ua_rate_pending = 0;
    ua_frame_valid = ua_paused = 0;
    ua_tx_bytes = ua_tx_slot = 0;
    sie_wr(S_INTRRX1E, sie_rd(S_INTRRX1E) & ~0x04u);
    sie_wr(S_INDEX, 2);
    sie_wr(S_RXCSR1, 0x90);
    sie_wr(S_TXCSR1, 0x48);
    sie_wr(S_INDEX, 3);
    sie_wr(S_TXCSR1, 0x48);
}

/* Keep one packet queued on each IN endpoint. Prepare capture's next packet
 * while DMA owns the current one, keeping PCM packing out of the critical
 * path between noticing completion and arming the next IN transfer.
 * An empty IN response loses a millisecond of the host's audio clock. */
static void ua_tx_fill(void)
{
    uint32_t n;
    if (ua.play_alt) {
        sie_wr(S_INDEX, 3);
        if (!(sie_rd(S_TXCSR1) & 1u)) {
            n = ua_feedback();
            ua_fb[0] = (uint8_t)n;
            ua_fb[1] = (uint8_t)(n >> 8);
            ua_fb[2] = (uint8_t)(n >> 16);
            fm1_usb_ep_send(3, ua_fb, 3);
            sie_wr(S_TXCSR1, 1);
        }
    }
    if (ua.cap_alt) {
        sie_wr(S_INDEX, 2);
        if (!(sie_rd(S_TXCSR1) & 1u)) {
            if (!ua_tx_bytes)                   /* first packet after a stream reset */
                ua_tx_bytes = ua_transmit(ua_tx[ua_tx_slot]);
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
    if (!usb.config || interface == UA_NO_IF || (interface != ua_if_play && interface != ua_if_cap) ||
        alt > (interface == ua_if_play ? 2u : 1u))   /* playback PCM16 / PCM24, capture PCM16 */
        return 0;
    if (UAC_BLOCKED())
        alt = 0;                                /* (SAFE MODE presents no audio function: belt and braces) */
    if (interface == ua_if_play) {
        ua_play_reset();
        ua.play_alt = (uint8_t)alt;
        if (alt)
            ua_play_config();
        else {
            sie_wr(S_INTRRX1E, sie_rd(S_INTRRX1E) & ~0x04u);
            sie_wr(S_INDEX, 2);
            sie_wr(S_RXCSR1, 0x90);
            sie_wr(S_INDEX, 3);
            sie_wr(S_TXCSR1, 0x48);
        }
    } else {
        ua_cap_reset();
        ua.cap_alt = (uint8_t)alt;
        if (alt)
            ua_cap_config();
        else {
            sie_wr(S_INDEX, 2);
            sie_wr(S_TXCSR1, 0x48);
            ua_tx_bytes = ua_tx_slot = 0;
        }
    }
    if (alt) {
        ua_tx_fill();                           /* ready for the host's first IN token */
        ua_tx_prepare();
    }
    return 1;
}

/* UAC1 endpoint sampling-frequency control. There is only one discrete rate;
 * retain these requests for hosts that set it while starting a stream.
 * Depth is selected by the AS alternate (playback: 1 = PCM16, 2 = packed PCM24; capture: 1 = PCM16). */
static int ua_control_setup(const uint8_t *s)
{
    uint32_t rate;
    if (!usb.config || s[2] || s[3] != 1u || s[5] || s[6] != 3u || s[7] ||
        (s[4] != 0x02u && s[4] != 0x82u) || (s[4] == 0x02u ? ua_if_play : ua_if_cap) == UA_NO_IF)
        return 0;                            /* (an endpoint of a function left out) */
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
    uint32_t csr, n, f;
    if (!usb.config)
        return;
    if (usb.suspended) {
        if (!ua_paused) {
            ua_play_reset();
            ua_cap_reset();
            if (ua.play_alt)
                ua_play_config();
            if (ua.cap_alt)
                ua_cap_config();
            ua_paused = 1;
            ua_frame_valid = 0;
        }
        return;
    }
    if (ua_paused) {
        if (ua.play_alt)
            ua_play_config();
        if (ua.cap_alt)
            ua_cap_config();
        ua_paused = 0;
    }
    if (!ua.play_alt && !ua.cap_alt) {
        ua_frame_valid = 0;                     /* idle frames are not missed ones */
        return;
    }
    ua_tx_fill();                              /* arm IN before unpacking playback */
    if (ua.play_alt) {
        sie_wr(S_INDEX, 2);
        csr = sie_rd(S_RXCSR1);
        if (csr & 1u) {
            n = sie_rd(S_RXCOUNT1) | sie_rd(S_RXCOUNT2) << 8;
            fm1_usb_rx_sync();
            if (csr & 0x0Cu)
                ua.bad_packets++;
            else
                ua_receive(ua_rx, n);
            sie_wr(S_RXCSR1, 0x10);             /* discard errors, release DMA packet */
        }
    }
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
        ua_sof();
    }
    ua_tx_fill();
    ua_tx_prepare();
}
