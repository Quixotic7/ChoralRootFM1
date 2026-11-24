/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the engine picker's readouts (rd1, firmware/src/rfm1_ext.h; FM1VST docs/RFM1.md "Readouts (rd1)"), on
 * the firmware as the emulator builds it, with FM1_HOST_EXT defined as an .rfm1 build defines it and the shim's
 * rfm1_readout_push replaced by a recorder:
 * - OPT held + PRESETS (cr_ui.c cu_epk_step): a readout at every detent, "Engine" and the engine under the cursor, the
 *   bar its place among the engines, while OPT is still held (the owner, 2026-11-23, on the Move: "Need the engine name
 *   to show up while turning"); the picker itself as before (its rank, nothing loaded until OPT's release);
 * - OPT's release (cu_epk_commit): the sound picked (the pool's RFM1_PRESET: the engine and the preset's name) last;
 * - OPT held + ALGORITHM: the bass part's, "Bass engine";
 * - the device build is untouched: without FM1_HOST_EXT the macro is ((void)0) (rfm1_ext.h), so the FM-1's image is
 *   the same bytes (checked by the size line and the goldens of ./build.sh, not here). */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../tools/emu/emu_hooks.h"

emu_hal_t emu_hal;
const int8_t emu_keymap[6][EMU_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};

/* the readouts pushed, as the shim would keep them */
typedef struct { int ctl, kind, frac; char title[17], value[17]; } rd_t;
static rd_t rd[64];
static unsigned nrd;
void rfm1_readout_push(int ctl, int kind, unsigned flags, int frac, const char *title, const char *value,
                       const char *sub);
void rfm1_readout_push(int ctl, int kind, unsigned flags, int frac, const char *title, const char *value,
                       const char *sub)
{
    (void)flags;
    (void)sub;
    if (nrd >= sizeof rd / sizeof rd[0])
        return;
    rd[nrd].ctl = ctl;
    rd[nrd].kind = kind;
    rd[nrd].frac = frac;
    snprintf(rd[nrd].title, sizeof rd[nrd].title, "%s", title ? title : "");
    snprintf(rd[nrd].value, sizeof rd[nrd].value, "%s", value ? value : "");
    nrd++;
}

#include "../tools/emu/emu_firmware.h"

static int fails, checks;
#define OK(c, ...)                                                      \
    do {                                                                \
        checks++;                                                       \
        if (!(c)) {                                                     \
            fails++;                                                    \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);               \
        } else {                                                        \
            printf("ok   ");                                            \
        }                                                               \
        printf(__VA_ARGS__);                                            \
        printf("\n");                                                   \
    } while (0)

static void dump(void)
{
    unsigned i;
    for (i = 0; i < nrd; i++)
        printf("    readout %u: ctl %d kind %d frac %d \"%s\" \"%s\"\n", i, rd[i].ctl, rd[i].kind, rd[i].frac,
               rd[i].title, rd[i].value);
}

/* OPT held, the part's knob turned by each of steps[], OPT released; returns 1 when every detent pushed the engine
 * under the cursor while OPT was down and nothing was loaded before the release */
static int pick(uint32_t part, const int32_t *steps, unsigned nsteps, const char *title, uint32_t *picked)
{
    uint32_t n, k, e0 = trk[part ? CR_PART_BASS : CR_PART_CHORD].eng_req % NENGINES;
    int good = 1;
    cu_engine_at(0, &n);
    nrd = 0;
    cu_btn_press(BT_OPT);
    for (k = 0; k < nsteps; k++) {
        unsigned before = nrd;
        uint32_t e;
        cu_knob(part ? EN_ALGO : EN_PRESET, steps[k]);
        e = cu_engine_at(cu.epk_rank, 0);
        if (nrd != before + 1u || rd[before].kind != RFM1_RD_PARAM || rd[before].ctl != -1 ||
            strcmp(rd[before].title, title) || strcmp(rd[before].value, ENGINES[e]->name) ||
            rd[before].frac != (n > 1u ? (int)(cu.epk_rank * 1000u / (n - 1u)) : -1) || cu.epk_on != part + 1u ||
            trk[part ? CR_PART_BASS : CR_PART_CHORD].eng_req % NENGINES != e0) {
            printf("  detent %u (%+d): %u readouts, the picker at %s\n", (unsigned)k, (int)steps[k], nrd - before,
                   ENGINES[e]->name);
            good = 0;
        }
    }
    *picked = cu_engine_at(cu.epk_rank, 0);
    cu_btn_release(BT_OPT);
    return good;
}

int main(void)
{
    static const int32_t STEPS[] = {+1, +1, -1, +2, -3, +1};
    uint32_t n, picked, before, e0;
    int good;

    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
    cu_engine_at(0, &n);

    /* the chord part: OPT held + PRESETS */
    cu_pool_load(0, ENGI_FM6, 1);
    e0 = trk[CR_PART_CHORD].eng_req % NENGINES;
    good = pick(0, STEPS, sizeof STEPS / sizeof STEPS[0], "Engine", &picked);
    before = nrd;
    OK(good && n > 1u && picked != e0, "OPT + PRESETS: a readout at each of %u detents while OPT is held (\"Engine\", the "
       "engine under the cursor, the bar its place among %u engines), nothing loaded before the release",
       (unsigned)(sizeof STEPS / sizeof STEPS[0]), (unsigned)n);
    OK(nrd > 0 && rd[nrd - 1].kind == RFM1_RD_PRESET && !strcmp(rd[nrd - 1].title, ENGINES[picked]->name) &&
       trk[CR_PART_CHORD].eng_req % NENGINES == picked && !cu.epk_on,
       "OPT released: the engine picked is loaded (%s) and its sound's readout comes last (%s / %s)", ENGINES[picked]->name,
       nrd ? rd[nrd - 1].title : "-", nrd ? rd[nrd - 1].value : "-");
    if (fails)
        dump();
    (void)before;

    /* the bass part: OPT held + ALGORITHM, on the home page */
    {
        static const int32_t B[] = {+1, -2, +1, +1};
        e0 = trk[CR_PART_BASS].eng_req % NENGINES;
        good = pick(1, B, sizeof B / sizeof B[0], "Bass engine", &picked);
        OK(good && cu_epk_ok(1), "OPT + ALGORITHM: the bass part's picker, a readout at each detent (\"Bass engine\")");
        OK(nrd > 0 && rd[nrd - 1].kind == RFM1_RD_PRESET && trk[CR_PART_BASS].eng_req % NENGINES == picked,
           "OPT released: the bass engine picked (%s, from %s), its sound's readout last", ENGINES[picked]->name,
           ENGINES[e0]->name);
        if (fails)
            dump();
    }

    /* a pick that lands on the engine it started from: no sound loaded (as before), the picker's readouts still came */
    {
        static const int32_t R[] = {+1, -1};
        uint32_t pos0;
        cu_pool_load(0, ENGI_FM6, 2);
        pos0 = cu_part_pos(0);
        good = pick(0, R, 2, "Engine", &picked);
        OK(good && picked == ENGI_FM6 && nrd == 2u && cu_part_pos(0) == pos0 && !cu.epk_on,
           "OPT + PRESETS there and back: two readouts (the neighbour, then FM6 again), the sound kept (FM6 %02u)",
           (unsigned)pos0);
        if (fails)
            dump();
    }

    printf("%s: %d of %d checks\n", fails ? "FAIL" : "PASS", checks - fails, checks);
    return fails ? 1 : 0;
}
