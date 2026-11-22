/* SPDX-License-Identifier: MPL-2.0 */
/* rfm1_ext.h: a firmware's words for the host it runs in (docs/RFM1.md "Readouts (rd1)"). One header a firmware vendors
 * (copy it into your tree; MPL-2.0, unlike the GPL fm1core.h). Built for the FM-1, or for any host without the
 * extension (FM1_HOST_EXT undefined: the default), every macro is ((void)0) and its arguments are not evaluated, so
 * the device image is the same bytes with the lines or without them. An .rfm1 built with fm1_add_rfm1(... WANTS rd1)
 * defines FM1_HOST_EXT and its shim keeps the latest readout for the host to show (a knob's name and value while it
 * turns, a sound's name, a message).
 *
 *   RFM1_READOUT(ctl, title, value)                 a parameter's name and value text ("Cutoff", "1.2 kHz")
 *   RFM1_READOUT_BAR(ctl, title, value, v, lo, hi)  .. with a bar: v in lo..hi
 *   RFM1_READOUT_SUB(ctl, title, value, sub, v, lo, hi)   .. the value in two parts ("120", "ms"), joined by a space
 *   RFM1_PRESET(title, name)                        a sound or preset ("Prophet", "Warm Pad")
 *   RFM1_MESSAGE(text)                              a message ("SAVED")
 *
 * ctl: the control it belongs to, as DYNAMIC-LABELS numbers them (0..6 the knobs SELECT ALGORITHM PRESETS K1..K4, 7
 * MASTER, 8..21 the buttons, 22..48 the keys), or -1: the host pairs it with the control in use. Text: printable
 * ASCII; at most 16 characters are kept of each. Call them from the firmware's main loop or UI (not from its audio). */
#ifndef RFM1_EXT_H
#define RFM1_EXT_H

#define RFM1_RD_PARAM 1
#define RFM1_RD_PRESET 2
#define RFM1_RD_MESSAGE 3
#define RFM1_RD_BIPOLAR 1u           /* flags: the bar from its middle */

#ifdef FM1_HOST_EXT
#include <stdint.h>
/* (the shim's, weak: a host build without it links, and the macros do nothing) */
void rfm1_readout_push(int ctl, int kind, unsigned flags, int frac, const char *title, const char *value,
                       const char *sub) __attribute__((weak));
#define RFM1_FRAC(v, lo, hi) \
    ((hi) > (lo) ? (int)((((int64_t)(v) - (int64_t)(lo)) * 1000) / ((int64_t)(hi) - (int64_t)(lo))) : -1)
#define RFM1_READOUT(ctl, title, value) \
    do { if (rfm1_readout_push) rfm1_readout_push((ctl), RFM1_RD_PARAM, 0u, -1, (title), (value), 0); } while (0)
#define RFM1_READOUT_BAR(ctl, title, value, v, lo, hi) \
    do { if (rfm1_readout_push) rfm1_readout_push((ctl), RFM1_RD_PARAM, 0u, RFM1_FRAC(v, lo, hi), (title), (value), 0); } while (0)
#define RFM1_READOUT_SUB(ctl, title, value, sub, v, lo, hi) \
    do { if (rfm1_readout_push) rfm1_readout_push((ctl), RFM1_RD_PARAM, 0u, RFM1_FRAC(v, lo, hi), (title), (value), (sub)); } while (0)
#define RFM1_PRESET(title, name) \
    do { if (rfm1_readout_push) rfm1_readout_push(-1, RFM1_RD_PRESET, 0u, -1, (title), (name), 0); } while (0)
#define RFM1_MESSAGE(text) \
    do { if (rfm1_readout_push) rfm1_readout_push(-1, RFM1_RD_MESSAGE, 0u, -1, "", (text), 0); } while (0)
#else
#define RFM1_READOUT(ctl, title, value) ((void)0)
#define RFM1_READOUT_BAR(ctl, title, value, v, lo, hi) ((void)0)
#define RFM1_READOUT_SUB(ctl, title, value, sub, v, lo, hi) ((void)0)
#define RFM1_PRESET(title, name) ((void)0)
#define RFM1_MESSAGE(text) ((void)0)
#endif

#endif
