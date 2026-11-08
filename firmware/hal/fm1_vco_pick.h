/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The VCO band kept after a full sweep (hal/fm1_ble.h fm1_ble_vco_sweep, HISTORY 3y247): pure, so the host tests it
 * (tests/cr_ble_test.c). low / high: bit b of word b / 32 set when band b's comparator read low / high. The first band
 * that reads neither; else at the flip, the last low band (keep 1) or the first high one (keep 2); else `captured`. */
#pragma once
#include <stdint.h>
static uint32_t fm1_vco_pick(const uint32_t low[2], const uint32_t high[2], uint32_t nbands, uint32_t keep,
                             uint32_t captured)
{
    uint32_t b, last_low = 0xFFu, first_high = 0xFFu;
    for (b = 0; b < nbands; b++) {
        uint32_t lo = low[b >> 5] >> (b & 31u) & 1u, hi = high[b >> 5] >> (b & 31u) & 1u;
        if (!lo && !hi)
            return b;
        if (lo)
            last_low = b;
        else if (first_high == 0xFFu)
            first_high = b;
    }
    b = keep == 2u ? first_high : last_low;
    return b != 0xFFu ? b : captured;
}
