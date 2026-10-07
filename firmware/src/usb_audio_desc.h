/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * ChoralRoot changes (six capture channels, PCM16 capture, the channel names) Copyright (C) 2026 ChoralRoot FM-1
 * contributors */
/* Melodee's two UAC1 functions (usb_audio_desc.h, Melodee 0.11.1): stereo playback, a six-channel recording;
 * 44.1 kHz. Hosts list them as two devices, each on its own clock: macOS times a single duplex device from its
 * recording packets, so a late one there cost playback.
 * IF2 control + IF3 streaming: playback "ChoralRoot Out", EP2 OUT, EP3 IN explicit feedback; alternate 1 = PCM16,
 * 2 = PCM24.
 * IF4 control + IF5 streaming: recording "ChoralRoot In", EP2 IN, alternate 1 = PCM16 (six channels: master L R,
 * CHORD L R, BASS L R; their names are strings 5..10, the input terminal's iChannelNames).
 * Included inside usb.c's CFG_DESC (after the MIDI function), whose length is CFG_LEN. */
    8, 0x0B, 2, 2, 1, 1, 0, 3,                      /* IAD: playback (IF 2-3), "ChoralRoot Out" */
    9, 4, 2, 0, 0, 1, 1, 0, 3,
    9, 0x24, 1, 0x00, 0x01, 30, 0, 1, 3,
    12, 0x24, 2, 1, 0x01, 0x01, 0, 2, 3, 0, 0, 0,  /* USB streaming input, L R */
    9, 0x24, 3, 2, 0x01, 0x03, 0, 1, 0,             /* speaker output */

    9, 4, 3, 0, 0, 1, 2, 0, 0,
    9, 4, 3, 1, 2, 1, 2, 0, 0,
    7, 0x24, 1, 1, 1, 1, 0,                         /* PCM */
    11, 0x24, 2, 1, 2, 2, 16, 1, 0x44, 0xAC, 0,
    9, 5, 0x02, 0x05, 180, 0, 1, 0, 131,            /* EP2 OUT isochronous async, 45 x 4 bytes */
    7, 0x25, 1, 1, 0, 0, 0,                         /* sampling-frequency control */
    9, 5, 0x83, 0x11, 3, 0, 1, 4, 0,               /* explicit 10.14 feedback */
    9, 4, 3, 2, 2, 1, 2, 0, 0,
    7, 0x24, 1, 1, 1, 1, 0,                         /* PCM */
    11, 0x24, 2, 1, 2, 3, 24, 1, 0x44, 0xAC, 0,
    9, 5, 0x02, 0x05, 14, 1, 1, 0, 131,             /* 45 x 6 bytes */
    7, 0x25, 1, 1, 0, 0, 0,                         /* sampling-frequency control */
    9, 5, 0x83, 0x11, 3, 0, 1, 4, 0,               /* explicit 10.14 feedback */

    8, 0x0B, 4, 2, 1, 1, 0, 4,                      /* IAD: recording (IF 4-5), "ChoralRoot In" */
    9, 4, 4, 0, 0, 1, 1, 0, 4,
    9, 0x24, 1, 0x00, 0x01, 30, 0, 1, 5,
    12, 0x24, 2, 3, 0x13, 0x07, 0, 6, 0, 0, 5, 0,  /* synthesizer: six non-spatial channels, names from string 5 */
    9, 0x24, 3, 4, 0x01, 0x01, 0, 3, 0,             /* USB streaming output */

    9, 4, 5, 0, 0, 1, 2, 0, 0,
    9, 4, 5, 1, 1, 1, 2, 0, 0,
    7, 0x24, 1, 4, 1, 1, 0,                         /* PCM */
    11, 0x24, 2, 1, 6, 2, 16, 1, 0x44, 0xAC, 0,
    9, 5, 0x82, 0x05, 28, 2, 1, 0, 0,               /* EP2 IN isochronous async, 45 x 12 bytes = 540 */
    7, 0x25, 1, 1, 0, 0, 0,                         /* sampling-frequency control */
