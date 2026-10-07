/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* FM6 patches outside the engine (eng_fm6.c): DX7 SysEx from USB-MIDI and back (fm6_service: main loop), and the
 * actions of the STORE page. The bank itself is fm6_bank.c.
 *
 * DX7 SysEx accepted on any channel n:
 *   F0 43 0n 00 01 1B <155 bytes> <checksum> F7    a voice (VCED) -> the FM6 track's patch
 *   F0 43 0n 09 20 00 <4096 bytes> <checksum> F7   32 voices (VMEM) -> the patch bank B1..B32, saved
 *   F0 43 1n gg pp dd F7                           a voice parameter pp + 128 gg (155: the six operator
 *                                                  switches, OP1 = bit 5) -> the track
 *   F0 43 1n 08 pp dd F7                           a function parameter (64 mono, 65 bend range, 66 step,
 *                                                  68 glissando, 69 portamento time, 70..77 wheel / foot /
 *                                                  breath / aftertouch range and target) -> the track's
 *                                                  function settings (saved with the project)
 *   F0 43 2n 00 F7 / F0 43 2n 09 F7                dump requests: the track's voice / the bank
 * The FM6 track: the selected track when it plays FM6, else track n + 1, else the first FM6 track.
 * Dexed or any DX7 librarian can so edit a track live and keep the banks.
 *
 * ChoralRoot: the FM6 part is the part the sound editor shows when it plays FM6, else the chord part's, else the
 * bass part's (the channel does not choose: ChoralRoot's parts are not channels); a voice received marks that sound
 * edited (SAVE keeps it in a user slot with its function settings, fm6_ustore.c). No STORE page: a bank dump fills
 * the bank B1..B32, PTCH loads from it. The dumps out are built in fm6_rx (the received frame is read by then; it
 * stays the main loop's until fm6_service lets it go): no second 4 KiB buffer. Included after cr_ui.c. */
#define fm6_buf fm6_rx                                   /* main loop: dumps out, in the frame just served */

static int fm6_is(uint32_t k) { return k < NPART && trk[k].eng_req == ENGI_FM6; }

static int fm6_sx_track(uint32_t ch)                     /* the FM6 part a DX7 message is for, -1 */
{
    (void)ch;
    if (cu.page == PG_EDIT && fm6_is(ce.part ? CR_PART_BASS : CR_PART_CHORD))
        return (int)(ce.part ? CR_PART_BASS : CR_PART_CHORD);
    if (fm6_is(CR_PART_CHORD))
        return (int)CR_PART_CHORD;
    return fm6_is(CR_PART_BASS) ? (int)CR_PART_BASS : -1;
}

static uint8_t fm6_chk(const uint8_t *p, uint32_t n)    /* DX7 checksum: data + it = 0 mod 128 */
{
    uint32_t s = 0;
    while (n--)
        s += *p++;
    return (uint8_t)(-s & 0x7Fu);
}

/* ------------------------------------------------------------- out --- */
#if FELUCCA_OTA
static void fm6_send_voice(uint32_t tr)                  /* VCED: the track's patch */
{
    static const uint8_t H[6] = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B};
    memcpy(fm6_buf, H, 6);
    memcpy(fm6_buf + 6, fm6_patch[tr % NTRK], 155);
    fm6_buf[161] = fm6_chk(fm6_buf + 6, 155);
    fm6_buf[162] = 0xF7;
    ota_wire_send(fm6_buf, 163);
}

static void fm6_send_bank(void)                          /* VMEM: the bank (an empty slot: the init voice) */
{
    static const uint8_t H[6] = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
    uint32_t k;
    memcpy(fm6_buf, H, 6);
    for (k = 0; k < FM6_BANK_N; k++)
        if (fm6_bank_get(k, fm6_buf + 6 + k * FM6_PACKED))
            memcpy(fm6_buf + 6 + k * FM6_PACKED, FM6_INIT, FM6_PACKED);
    fm6_buf[4102] = fm6_chk(fm6_buf + 6, 4096);
    fm6_buf[4103] = 0xF7;
    ota_wire_send(fm6_buf, FM6_RX);
}
#endif

/* ------------------------------------------------------------- in --- */
static void fm6_sysex(const uint8_t *b, uint32_t n)
{
    uint32_t i, st = b[2] & 0xF0u, ch = b[2] & 15u;
    int tr = fm6_sx_track(ch);
    char nm[12];
    if (n == 163u && st == 0x00u && b[3] == 0x00 && b[4] == 0x01 && b[5] == 0x1B &&
        fm6_chk(b + 6, 155) == b[161]) {                 /* one voice */
        if (tr < 0) {
            ui_message("FM6: NO FM6 PART");
            return;
        }
        fm6_put_patch((uint32_t)tr, b + 6, 1);           /* a new voice: the notes stop, as in Dexed */
        fm6_slot[tr] = (uint8_t)trk[tr].p[P_E7];         /* (fm6_poll: the patch stays the track's own) */
        fm6_name(nm, fm6_patch[tr]);
        ui_say("FM6 VOICE ", nm);
        if (tr < 2)
            cu_edited((uint32_t)tr);                     /* (the part's sound: SAVE keeps the voice) */
        ui.force = 1;
    } else if (n == FM6_RX && st == 0x00u && b[3] == 0x09 && b[4] == 0x20 && b[5] == 0x00 &&
               fm6_chk(b + 6, 4096) == b[4102]) {        /* 32 voices */
        uint8_t rec[FM6_PACKED];
        if (transport_busy()) {                          /* no flash erase while playing */
            ui_message("STOP TO SAVE");
            return;
        }
        if (!fm6_bank_valid(&fm6_bank))
            fm6_bank_empty();
        for (i = 0; i < FM6_BANK_N; i++) {
            uint32_t j;
            for (j = 0; j < FM6_PACKED; j++)
                rec[j] = b[6 + i * FM6_PACKED + j] & 0x7Fu;
            fm6_pack7(fm6_bank.pk[i], rec, FM6_PACKED);
        }
        fm6_bank.used = 0xFFFFFFFFu;
        fm6_bank_read = fm6_bank_get;
        fm6_bank_gen++;
        for (i = 0; i < NTRK; i++)                       /* tracks on B..: the new patches (fm6_poll) */
            if (fm6_slot[i] >= FM6_NFAC && fm6_slot[i] != 0xFFu)
                fm6_slot[i] = 0xFFu;
        ui_message(fm6_bank_save() ? "FM6 BANK (RAM)" : "FM6 BANK SAVED");
        ui.force = 1;
    } else if (n == 7u && st == 0x10u && !(b[3] >> 2) && tr >= 0) {   /* a voice parameter */
        uint32_t k = (uint32_t)(b[3] & 3u) << 7 | b[4];
        if (k < 155u) {
            uint8_t v[FP_SIZE + 1u];
            memcpy(v, fm6_patch[tr], FP_SIZE);
            v[k] = b[5];
            fm6_put_patch((uint32_t)tr, v, 0);
            if (tr < 2)
                cu_edited((uint32_t)tr);
        } else if (k == 155u) {
            fm6_on[tr] = b[5] & FM6_ON_ALL;              /* bit 0 OP6 .. bit 5 OP1, as fm6_on */
        }
        ui.force = 1;
    } else if (n == 7u && st == 0x10u && b[3] == 0x08u && tr >= 0) {   /* a function parameter: the track's */
        uint32_t v = b[5], f = (uint32_t)tr;
        switch (b[4]) {
        case 64:                                         /* MONO: Dexed's (legato, the highest key) */
            trk[tr].p[P_VOICE] = v ? V_LEGATO : V_POLY;
            if (v)
                trk[tr].p[P_PRIO] = 2;
            break;
        case 65:
            fm6_fn_set(f, FN_PBUP, (int32_t)v);
            fm6_fn_set(f, FN_PBDN, (int32_t)v);
            break;
        case 66: fm6_fn_set(f, FN_PBSTEP, (int32_t)v); break;
        case 68: fm6_fn_set(f, FN_GLISS, (int32_t)v); break;
        case 69: fm6_fn_set(f, FN_PTIME, (int32_t)(v > 99u ? 99u : v) * 127 / 99); break;   /* 0..99 -> CC 5 */
        default:
            if (b[4] >= 70u && b[4] <= 77u)              /* range, target: wheel, foot, breath, aftertouch */
                fm6_fn_set(f, FN_MWR + (uint32_t)(b[4] - 70u), (int32_t)v);
            break;
        }
        ui.force = 1;
    } else if (n == 5u && st == 0x20u) {                 /* dump requests */
#if FELUCCA_OTA
        if (b[3] == 0x00 && tr >= 0)
            fm6_send_voice((uint32_t)tr);
        else if (b[3] == 0x09)
            fm6_send_bank();
#endif
    }
}

static void fm6_service(void)                            /* main loop: a DX7 frame from USB-MIDI */
{
    if (fm6_rx_ready) {
        RING_PUBLISH();                                  /* read the frame only after the flag */
        fm6_sysex(fm6_rx, fm6_rx_n);
        RING_PUBLISH();
        fm6_rx_ready = 0;
    }
}
