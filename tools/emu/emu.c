/* SPDX-License-Identifier: GPL-3.0-only */
/* The M-VAVE FM-1 emulator for the Mac: the firmware (emu_fw.c, through emu_hooks.h only) driven from
 * the keyboard and the mouse, with its LCD, LEDs, audio (SDL2) and MIDI (CoreMIDI).
 *   build: sh tools/emu/build.sh      run: build/host/emu [--help]
 * The window: the 240x240 LCD scaled (x3, nearest neighbour), below it the FM-1 panel drawn in the
 * geometry of ChoralRootFM1Designer/index.html (KEYS, BUTTONS, ENCODERS, SCREEN, BODY; 904 x 566 units). */
#include <SDL.h>
#include <errno.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <sys/stat.h>
#include "emu.h"
#include "emu_hooks.h"

/* ================================================================== options === */
static int opt_front, opt_scale = 0, opt_demo, opt_midi = 1, opt_midi_log, opt_audio = 1;
static int opt_no_flash, opt_save_on_exit;
static const char *opt_flash;               /* the flash image file (emu_fw_options) */
static double opt_bench = 0, opt_quit_after = 0;
static const char *opt_script, *opt_wav, *opt_shot;
static int opt_frames;
static char out_dir[1024];                       /* build/emu/ */

/* ================================================================ geometry === */
/* ChoralRootFM1Designer/index.html, drawing units (904 x 566) */
typedef struct { float x, y, w, h; } rect_t;
static const float WHITE_X[16] = {71, 119, 167, 215, 264, 312, 360, 408, 456, 504, 553, 601, 649, 697, 745, 794};
static const float BLACK_X[11] = {95, 143, 191, 288, 336, 432, 480, 529, 625, 673, 769};
#define KEY_W 42.f
#define KEY_H 76.f
static rect_t KEYR[EMU_NKEY];
static int key_black(int k) { return (0x54A >> ((k + 5) % 12)) & 1; }       /* seq.c key_black */
static const float BTN_X[6] = {500, 554, 608, 661, 715, 768}, BTN_Y[2] = {177, 231};
#define BTN_S 36.f
static rect_t BTNR[EMU_NB];
/* knobs in EMU_E_* order: SELECT ALGORITHM PRESETS KNOB1..4 MASTER */
static const float ENC_CX[EMU_NE] = {186.5f, 186.5f, 95, 508, 604, 701, 797, 95};
static const float ENC_CY[EMU_NE] = {101, 191, 191, 101, 101, 101, 101, 101};
static const float ENC_LY[EMU_NE] = {66, 156, 156, 66, 66, 66, 66, 66};
#define ENC_R 21.f
#define ENC_CAP 12.f
static const rect_t BODY = {22, 22, 860, 522}, SCREEN = {247, 59, 201, 201}, SCREEN_A = {272, 84, 150, 150};
static const rect_t VIEW = {14, 14, 876, 538};   /* the part of the drawing shown */

static void geometry_init(void)
{
    int k, w = 0, b = 0;
    for (k = 0; k < EMU_NKEY; k++) {
        int bl = key_black(k);
        KEYR[k] = (rect_t){bl ? BLACK_X[b] : WHITE_X[w], bl ? 326.f : 408.f, KEY_W, KEY_H};
        if (bl) b++; else w++;
    }
    for (k = 0; k < 12; k++)
        BTNR[k] = (rect_t){BTN_X[k % 6], BTN_Y[k / 6], BTN_S, BTN_S};
    BTNR[EMU_B_OCTDN] = (rect_t){87, 250, 42, 22};
    BTNR[EMU_B_OCTUP] = (rect_t){153, 250, 42, 22};
}

/* ================================================================== raster === */
typedef struct { uint32_t *p; int w, h; float k, ox, oy; } canvas_t;   /* px = (unit - o) * k */
static inline float clampf(float v, float a, float b) { return v < a ? a : v > b ? b : v; }
static inline void blend(canvas_t *c, int x, int y, uint32_t rgb, float a)
{
    uint32_t *d, s, r, g, b;
    if (a <= 0.f || x < 0 || y < 0 || x >= c->w || y >= c->h)
        return;
    if (a > 1.f) a = 1.f;
    d = &c->p[y * c->w + x];
    s = *d;
    r = (uint32_t)(((s >> 16) & 255u) + (((rgb >> 16) & 255u) - (float)((s >> 16) & 255u)) * a);
    g = (uint32_t)(((s >> 8) & 255u) + (((rgb >> 8) & 255u) - (float)((s >> 8) & 255u)) * a);
    b = (uint32_t)((s & 255u) + ((rgb & 255u) - (float)(s & 255u)) * a);
    *d = 0xFF000000u | r << 16 | g << 8 | b;
}
/* signed distance (pixels) of a rounded box; mode 0 fill, else a stroke lw px wide inside the edge */
static void rrect_px(canvas_t *c, float x0, float y0, float w, float h, float r, uint32_t rgb, float a, float lw)
{
    int x, y, xa = (int)floorf(x0) - 1, ya = (int)floorf(y0) - 1, xb = (int)ceilf(x0 + w) + 1, yb = (int)ceilf(y0 + h) + 1;
    float cx = x0 + w / 2, cy = y0 + h / 2, hx = w / 2, hy = h / 2;
    r = clampf(r, 0, fminf(hx, hy));
    for (y = ya; y <= yb; y++)
        for (x = xa; x <= xb; x++) {
            float qx = fabsf(x + .5f - cx) - (hx - r), qy = fabsf(y + .5f - cy) - (hy - r);
            float ox = fmaxf(qx, 0), oy = fmaxf(qy, 0);
            float d = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0) - r, cov;
            if (lw > 0)
                cov = clampf(.5f - (fabsf(d + lw / 2) - lw / 2), 0, 1);
            else
                cov = clampf(.5f - d, 0, 1);
            if (cov > 0)
                blend(c, x, y, rgb, cov * a);
        }
}
static void rrect(canvas_t *c, rect_t r, float rad, uint32_t rgb, float a)
{
    rrect_px(c, (r.x - c->ox) * c->k, (r.y - c->oy) * c->k, r.w * c->k, r.h * c->k, rad * c->k, rgb, a, 0);
}
static void rrect_line(canvas_t *c, rect_t r, float rad, uint32_t rgb, float a, float lw)
{
    rrect_px(c, (r.x - c->ox) * c->k, (r.y - c->oy) * c->k, r.w * c->k, r.h * c->k, rad * c->k, rgb, a, fmaxf(lw * c->k, 1.f));
}
static void circle(canvas_t *c, float cx, float cy, float r, uint32_t rgb, float a)
{
    rrect(c, (rect_t){cx - r, cy - r, 2 * r, 2 * r}, r, rgb, a);
}
static void ring(canvas_t *c, float cx, float cy, float r, uint32_t rgb, float a, float lw, float dash)
{   /* a circle line (centred on r), dashed: dash units on, dash*1.1 off */
    float px = (cx - c->ox) * c->k, py = (cy - c->oy) * c->k, R = r * c->k, W = fmaxf(lw * c->k, 1.f);
    int x, y;
    for (y = (int)(py - R - W) - 1; y <= (int)(py + R + W) + 1; y++)
        for (x = (int)(px - R - W) - 1; x <= (int)(px + R + W) + 1; x++) {
            float dx = x + .5f - px, dy = y + .5f - py, d = fabsf(sqrtf(dx * dx + dy * dy) - R) - W / 2;
            float cov = clampf(.5f - d, 0, 1);
            if (cov > 0 && dash > 0) {
                float s = (atan2f(dy, dx) + (float)M_PI) * r;   /* arc length in units */
                if (fmodf(s, dash * 2.1f) > dash)
                    cov = 0;
            }
            blend(c, x, y, rgb, cov * a);
        }
}
static void segment(canvas_t *c, float x1, float y1, float x2, float y2, float lw, uint32_t rgb, float a)
{   /* a line with round caps */
    float ax = (x1 - c->ox) * c->k, ay = (y1 - c->oy) * c->k, bx = (x2 - c->ox) * c->k, by = (y2 - c->oy) * c->k;
    float W = lw * c->k / 2, vx = bx - ax, vy = by - ay, L2 = vx * vx + vy * vy;
    int x, y;
    for (y = (int)(fminf(ay, by) - W) - 1; y <= (int)(fmaxf(ay, by) + W) + 1; y++)
        for (x = (int)(fminf(ax, bx) - W) - 1; x <= (int)(fmaxf(ax, bx) + W) + 1; x++) {
            float px = x + .5f - ax, py = y + .5f - ay, t = L2 > 0 ? clampf((px * vx + py * vy) / L2, 0, 1) : 0;
            float dx = px - t * vx, dy = py - t * vy;
            blend(c, x, y, rgb, clampf(.5f - (sqrtf(dx * dx + dy * dy) - W), 0, 1) * a);
        }
}

/* 5x7 font, ASCII 32..95, columns, bit 0 = top */
static const uint8_t FONT[64][5] = {
    {0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x5F,0x00,0x00},{0x00,0x07,0x00,0x07,0x00},{0x14,0x7F,0x14,0x7F,0x14},
    {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,0x08,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0x00,0x05,0x03,0x00,0x00},
    {0x00,0x1C,0x22,0x41,0x00},{0x00,0x41,0x22,0x1C,0x00},{0x08,0x2A,0x1C,0x2A,0x08},{0x08,0x08,0x3E,0x08,0x08},
    {0x00,0x50,0x30,0x00,0x00},{0x08,0x08,0x08,0x08,0x08},{0x00,0x60,0x60,0x00,0x00},{0x20,0x10,0x08,0x04,0x02},
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
    {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
    {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},{0x00,0x36,0x36,0x00,0x00},{0x00,0x56,0x36,0x00,0x00},
    {0x08,0x14,0x22,0x41,0x00},{0x14,0x14,0x14,0x14,0x14},{0x00,0x41,0x22,0x14,0x08},{0x02,0x01,0x51,0x09,0x06},
    {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x01,0x01},{0x3E,0x41,0x41,0x51,0x32},
    {0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},
    {0x7F,0x40,0x40,0x40,0x40},{0x7F,0x02,0x04,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
    {0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x7F,0x20,0x18,0x20,0x7F},
    {0x63,0x14,0x08,0x14,0x63},{0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},{0x00,0x7F,0x41,0x41,0x00},
    {0x02,0x04,0x08,0x10,0x20},{0x00,0x41,0x41,0x7F,0x00},{0x04,0x02,0x01,0x02,0x04},{0x40,0x40,0x40,0x40,0x40},
};
static int font_px(int ch, int col, int row)
{
    if (ch >= 'a' && ch <= 'z')
        ch -= 32;
    if (ch < 32 || ch > 95 || col < 0 || col > 4 || row < 0 || row > 6)
        return 0;
    return (FONT[ch - 32][col] >> row) & 1;
}
/* text centred on cx, cap height = size units, top at y (units); 3x3 supersampled */
static void text(canvas_t *c, float cx, float y, float size, const char *s, uint32_t rgb, float a)
{
    int n = (int)strlen(s), x, yy;
    float f = size / 7.f, w = (n * 6 - 1) * f, x0 = cx - w / 2;
    int xa = (int)((x0 - c->ox) * c->k) - 1, xb = (int)((x0 + w - c->ox) * c->k) + 1;
    int ya = (int)((y - c->oy) * c->k) - 1, yb = (int)((y + size - c->oy) * c->k) + 1;
    for (yy = ya; yy <= yb; yy++)
        for (x = xa; x <= xb; x++) {
            int sx, sy, hit = 0;
            for (sy = 0; sy < 3; sy++)
                for (sx = 0; sx < 3; sx++) {
                    float ux = (x + (sx + .5f) / 3.f) / c->k + c->ox - x0, uy = (yy + (sy + .5f) / 3.f) / c->k + c->oy - y;
                    int gx, gy, ci;
                    if (ux < 0 || uy < 0)
                        continue;
                    gx = (int)(ux / f);
                    gy = (int)(uy / f);
                    ci = gx / 6;
                    if (ci < n)
                        hit += font_px((unsigned char)s[ci], gx % 6, gy);
                }
            if (hit)
                blend(c, x, yy, rgb, a * hit / 9.f);
        }
}

/* ============================================================== panel state === */
enum { SRC_KEY = 1, SRC_ESC = 2, SRC_MOUSE = 4, SRC_LATCH = 8, SRC_SCRIPT = 16 };
static uint8_t key_src[EMU_NKEY], btn_src[EMU_NB];
static int sel_knob = EMU_E_PRESETS;
static float knob_angle[EMU_NE];
static uint8_t led_pos[41];                      /* id -> (col << 3) | row, 0xFF none */

static void hal_publish(void)
{
    uint32_t k = 0, b = 0;
    int i;
    for (i = 0; i < EMU_NKEY; i++)
        if (key_src[i])
            k |= 1u << i;
    for (i = 0; i < EMU_NB; i++)
        if (btn_src[i])
            b |= 1u << emu_hal.btn_id[i];
    emu_hal.keys = k;
    emu_hal.buttons = b;
}
static void hold_key(int k, int src, int down)
{
    if (k < 0 || k >= EMU_NKEY)
        return;
    if (down && !key_src[k])
        __atomic_fetch_or(&emu_hal.keys_tap, 1u << k, __ATOMIC_SEQ_CST);
    key_src[k] = (uint8_t)(down ? key_src[k] | src : key_src[k] & ~src);
    hal_publish();
}
static void hold_btn(int b, int src, int down)
{
    if (b < 0 || b >= EMU_NB)
        return;
    if (down && !btn_src[b])
        __atomic_fetch_or(&emu_hal.buttons_tap, 1u << emu_hal.btn_id[b], __ATOMIC_SEQ_CST);
    btn_src[b] = (uint8_t)(down ? btn_src[b] | src : btn_src[b] & ~src);
    hal_publish();
}
static void release_src(int src)
{
    int i;
    for (i = 0; i < EMU_NKEY; i++)
        key_src[i] = (uint8_t)(key_src[i] & ~src);
    for (i = 0; i < EMU_NB; i++)
        btn_src[i] = (uint8_t)(btn_src[i] & ~src);
    hal_publish();
}
static void turn(int role, int steps)
{
    if (!steps || role < 0 || role >= EMU_NE)
        return;
    if (role == EMU_E_MASTER) {
        int m = emu_hal.master + steps * 16;
        emu_hal.master = m < 0 ? 0 : m > 1023 ? 1023 : m;
        return;
    }
    __atomic_fetch_add(&emu_hal.enc[emu_hal.enc_id[role]], steps * emu_hal.enc_dir[role], __ATOMIC_SEQ_CST);
    knob_angle[role] += steps * (float)(2 * M_PI / 24);
}
static int led_state(int id)                     /* 0 off, 1 dim, 2 lit */
{
    uint8_t q = id >= 0 && id < 41 ? led_pos[id] : 0xFF;
    if (q == 0xFF)
        return 0;
    if ((emu_hal.led[q >> 3] >> (q & 7)) & 1u)
        return 2;
    return ((emu_hal.led_dim[q >> 3] >> (q & 7)) & 1u) ? 1 : 0;
}

/* ================================================================== drawing === */
#define C_PLATE 0x1C1C20u
#define C_BED 0x141417u
#define C_EDGE 0x2A2A30u
#define C_CAP 0x2B2B31u
#define C_LEDOFF 0x1A1A1Eu
#define C_LABEL 0x8A8A92u
#define C_HINT 0x54D678u
#define C_WHITE 0xF4F4FFu
#define C_RED 0xFF4242u
#define C_ORANGE 0xFFA52Au
#define C_GREEN 0x3DDC5Au
static canvas_t base, cv;

static void draw_base(void)
{
    int i;
    canvas_t *c = &base;
    for (i = 0; i < c->w * c->h; i++)
        c->p[i] = 0xFF0E0E10u;
    rrect(c, BODY, 38, C_PLATE, 1);
    rrect_line(c, BODY, 38, 0x000000, 1, 1.5f);
    rrect_line(c, (rect_t){BODY.x + 7, BODY.y + 7, BODY.w - 14, BODY.h - 14}, 32, C_EDGE, 1, 1);
    rrect(c, (rect_t){52, 306, 800, 198}, 20, C_BED, 1);
    rrect_line(c, (rect_t){52, 306, 800, 198}, 20, C_EDGE, 1, 1);
    rrect(c, (rect_t){484, 162, 338, 124}, 8, C_BED, 1);
    rrect_line(c, (rect_t){484, 162, 338, 124}, 8, C_EDGE, 1, 1);
    rrect(c, (rect_t){77, 243, 127, 36}, 6, C_BED, 1);
    rrect_line(c, (rect_t){77, 243, 127, 36}, 6, C_EDGE, 1, 1);
    {
        static const float DX[4] = {262, 406, 599, 743};
        for (i = 0; i < 4; i++)
            circle(c, DX[i], 404, 2, 0x3A3A42u, 1);
    }
    for (i = 0; i < EMU_NE; i++) {
        const char *h = keymap_hint(KM_SELECT, i);
        ring(c, ENC_CX[i], ENC_CY[i], ENC_R - 3, 0x3A3A42u, 1, 2.5f, 2);
        text(c, ENC_CX[i], ENC_LY[i] - 6, 7, EMU_ENC_NAME[i], C_LABEL, 1);
        if (h)
            text(c, ENC_CX[i], ENC_CY[i] + ENC_R + 6, 7, h, C_HINT, .9f);
    }
    rrect(c, SCREEN, 10, 0x0B0B0Eu, 1);
    rrect_line(c, SCREEN, 10, 0x000000, 1, 1);
}

static void draw_panel(void)
{
    int k, i;
    char n[8];
    const char *h;
    canvas_t *c = &cv;
    memcpy(cv.p, base.p, (size_t)cv.w * (size_t)cv.h * 4u);
    for (k = 0; k < EMU_NKEY; k++) {              /* keys: the cap, the LED bar */
        rect_t r = KEYR[k];
        float cx = r.x + KEY_W / 2;
        int lv = led_state(14 + k);
        rrect(c, r, 21, key_src[k] ? 0x45454Eu : C_CAP, 1);
        rrect_line(c, r, 21, key_src[k] ? C_HINT : 0x0A0A0Cu, 1, key_src[k] ? 1.5f : 1);
        if (lv == 2)
            rrect(c, (rect_t){cx - 9, r.y + 9, 18, 40}, 9, C_WHITE, .16f);
        rrect(c, (rect_t){cx - 4, r.y + 14, 8, 30}, 4, C_LEDOFF, 1);
        if (lv)
            rrect(c, (rect_t){cx - 4, r.y + 14, 8, 30}, 4, C_WHITE, lv == 2 ? 1.f : .38f);
        emu_note_name(k, n);
        text(c, cx, r.y + 50, 6.5f, n, 0x8A8A92u, 1);
        if ((h = keymap_hint(KM_KEY, k)) != NULL)
            text(c, cx, r.y + 61, 6.5f, h, C_HINT, .9f);
    }
    for (i = 0; i < EMU_NB; i++) {                /* buttons: the cap lit in its LED's colour */
        rect_t r = BTNR[i];
        int lv = led_state(emu_hal.btn_id[i]), oct = i >= EMU_B_OCTDN;
        uint32_t col = i == EMU_B_REC ? C_RED : i == EMU_B_PLAY ? C_ORANGE : C_WHITE;
        h = i == EMU_B_OCTDN ? "LEFT" : i == EMU_B_OCTUP ? "RIGHT" : keymap_hint(KM_BTN, i);
        rrect(c, r, oct ? 6 : 5, C_CAP, 1);
        if (lv)
            rrect(c, r, oct ? 6 : 5, col, lv == 2 ? 1.f : .38f);
        rrect_line(c, r, oct ? 6 : 5, btn_src[i] ? C_HINT : 0x0A0A0Cu, 1, btn_src[i] ? 2 : 1);
        text(c, r.x + r.w / 2, r.y + (oct ? 7.5f : 11), oct ? 6.5f : 7.5f, EMU_BTN_NAME[i],
             lv == 2 ? 0x111114u : 0xC8C8D0u, 1);
        if (h && !oct)
            text(c, r.x + r.w / 2, r.y + 25, 5, h, lv == 2 ? 0x1E3A26u : C_HINT, .9f);
        else if (h)
            text(c, r.x + r.w / 2, r.y + r.h + 4, 5, h, C_HINT, .9f);
        if (i == EMU_B_PLAY) {                    /* the green LED */
            uint8_t q = emu_hal.led_play_green;
            int on = (emu_hal.led[q >> 3] >> (q & 7)) & 1u;
            circle(c, r.x + 6, r.y + r.h - 6, 3.2f, on ? C_GREEN : C_LEDOFF, 1);
        }
    }
    for (i = 0; i < EMU_NE; i++) {               /* knobs */
        float cx = ENC_CX[i], cy = ENC_CY[i], a = knob_angle[i];
        if (i == EMU_E_MASTER)
            a = (float)((emu_hal.master / 1023.0 - .5) * 1.5 * M_PI);
        circle(c, cx, cy, ENC_R, 0x26262Cu, 1);
        ring(c, cx, cy, ENC_R - 3, 0x3A3A42u, 1, 2.5f, 2);
        circle(c, cx, cy, ENC_CAP, 0x35353Cu, 1);
        segment(c, cx + 4 * sinf(a), cy - 4 * cosf(a), cx + (ENC_CAP - 1) * sinf(a), cy - (ENC_CAP - 1) * cosf(a), 2,
                i == EMU_E_MASTER ? 0xFFFFFFu : 0xC8C8D0u, 1);
        if (i == sel_knob)
            ring(c, cx, cy, ENC_R + 3, C_HINT, 1, 1.5f, 0);
    }
}

/* ================================================================= timing === */
static double perf_us(void) { return (double)SDL_GetPerformanceCounter() * 1e6 / (double)SDL_GetPerformanceFrequency(); }
static struct {
    uint64_t blocks, over_budget, over_half, cbs, over_warm;
    uint64_t nz_blocks, nz_samples;              /* blocks with a non-zero sample, non-zero samples */
    double sum_us, max_us, max_warm_us, cb_gap_max, last_cb;
} ast;
static struct { uint64_t frames; double sum_us, max_us, first, last; } ust;
#define BLOCK_US (EMU_BLOCK * 1e6 / EMU_FS)

static void render_block(int16_t *out)
{
    double t0 = perf_us(), d;
    emu_fw_audio(out, EMU_BLOCK);
    d = perf_us() - t0;
    ast.blocks++;
    ast.sum_us += d;
    if (d > ast.max_us)
        ast.max_us = d;
    if (ast.blocks > EMU_FS / EMU_BLOCK && d > ast.max_warm_us)
        ast.max_warm_us = d;
    if (d > BLOCK_US)
        ast.over_budget++;
    if (d > BLOCK_US && ast.blocks > EMU_FS / EMU_BLOCK)
        ast.over_warm++;
    if (d > BLOCK_US / 2)
        ast.over_half++;
    {
        int i, nz = 0;
        for (i = 0; i < EMU_BLOCK * 2; i++)
            nz += out[i] != 0;
        ast.nz_samples += (uint64_t)nz;
        ast.nz_blocks += nz != 0;
    }
}

static int16_t fifo[EMU_BLOCK * 2];
static int fifo_n, fifo_pos;
static FILE *wav;
static uint32_t wav_frames;
static void audio_cb(void *u, Uint8 *stream, int len)
{
    int16_t *o = (int16_t *)stream;
    int frames = len / 4;
    double now = perf_us();
    (void)u;
    if (ast.cbs++ && now - ast.last_cb > ast.cb_gap_max)
        ast.cb_gap_max = now - ast.last_cb;
    ast.last_cb = now;
    while (frames > 0) {
        int n;
        if (!fifo_n) {
            render_block(fifo);
            fifo_n = EMU_BLOCK;
            fifo_pos = 0;
        }
        n = frames < fifo_n ? frames : fifo_n;
        memcpy(o, fifo + 2 * fifo_pos, (size_t)n * 4u);
        o += 2 * n;
        frames -= n;
        fifo_pos += n;
        fifo_n -= n;
    }
}

static void print_stats(void)
{
    uint32_t shed, cpu;
    emu_fw_stats(&shed, &cpu);
    if (ast.blocks)
        printf("audio: %llu blocks of %d frames (budget %.0f us each): render avg %.1f us, max %.1f us "
               "(after the first second %.1f us); over half the budget %llu, over budget %llu (after the first second %llu)\n",
               (unsigned long long)ast.blocks, EMU_BLOCK, BLOCK_US, ast.sum_us / (double)ast.blocks, ast.max_us,
               ast.max_warm_us, (unsigned long long)ast.over_half, (unsigned long long)ast.over_budget,
               (unsigned long long)ast.over_warm);
    if (ast.blocks)
        printf("audio: non-silent blocks %llu of %llu, non-zero samples %llu\n", (unsigned long long)ast.nz_blocks,
               (unsigned long long)ast.blocks, (unsigned long long)ast.nz_samples);
    if (ast.cbs > 1)
        printf("audio: %llu device callbacks, longest gap %.0f us\n", (unsigned long long)ast.cbs, ast.cb_gap_max);
    if (ust.frames)
        printf("ui: %llu frames, one every %.1f ms; ui_input+ui_leds+ui_draw avg %.0f us, max %.0f us\n",
               (unsigned long long)ust.frames,
               ust.frames > 1 ? (ust.last - ust.first) / 1000.0 / (double)(ust.frames - 1) : 0.0,
               ust.sum_us / (double)ust.frames, ust.max_us);
    printf("firmware: voices shed on overload %u, CPU meter %u %%; CPU lock: the UI held it at most %u us, "
           "the audio waited for it at most %u us\n", shed, cpu, emu_hal.ui_lock_max_us, emu_hal.audio_wait_max_us);
}

static void ui_frame(double now_us)
{
    double t0 = perf_us(), d;
    emu_fw_frame();
    d = perf_us() - t0;
    if (!ust.frames)
        ust.first = now_us;
    ust.last = now_us;
    ust.frames++;
    ust.sum_us += d;
    if (d > ust.max_us)
        ust.max_us = d;
}

/* ================================================================ images === */
static int recording, shot_ppm;
static uint32_t rec_n;
static int has_ext(const char *s, const char *e)
{
    size_t n = strlen(s), m = strlen(e);
    return n >= m && !strcasecmp(s + n - m, e);
}
static void mkdir_parent(const char *path)       /* mkdir -p of the file's directory */
{
    char d[1200];
    char *q;
    snprintf(d, sizeof d, "%s", path);
    for (q = d + 1; *q; q++)
        if (*q == '/') {
            *q = 0;
            mkdir(d, 0755);
            *q = '/';
        }
}
/* the LCD to a file: PPM if the name ends in .ppm, else PNG */
static int write_lcd(const char *path)
{
    int r;
    mkdir_parent(path);
    r = has_ext(path, ".ppm") ? emu_write_ppm(path, emu_hal.lcd, EMU_LCD_W, EMU_LCD_H)
                              : emu_write_png(path, emu_hal.lcd, EMU_LCD_W, EMU_LCD_H);
    printf(r == 0 ? "screenshot: %s\n" : "screenshot: cannot write %s\n", path);
    return r;
}
static void shot(const char *name)
{
    char p[1200];
    if (name && (strchr(name, '/') || has_ext(name, ".ppm") || has_ext(name, ".png"))) {
        write_lcd(name);                          /* a path (relative to the current directory) */
        return;
    }
    if (name) {
        snprintf(p, sizeof p, "%s%s.png", out_dir, name);
    } else {
        struct stat st;
        int i;
        for (i = 0; i < 1000; i++) {
            snprintf(p, sizeof p, "%sshot_%03d.png", out_dir, i);
            if (stat(p, &st) != 0)
                break;
        }
    }
    write_lcd(p);
    if (shot_ppm) {                               /* --headless: a PPM beside it */
        size_t n = strlen(p);
        memcpy(p + n - 3, "ppm", 3);
        write_lcd(p);
    }
}
static void record_frame(void)
{
    char p[1200];
    snprintf(p, sizeof p, "%srec/%04u.ppm", out_dir, rec_n++);
    emu_write_ppm(p, emu_hal.lcd, EMU_LCD_W, EMU_LCD_H);
}
static void toggle_record(void)
{
    char p[1200];
    recording = !recording;
    if (recording) {
        snprintf(p, sizeof p, "%srec", out_dir);
        mkdir(p, 0755);
        rec_n = 0;
        printf("recording LCD frames to %s/\n", p);
    } else {
        printf("recording stopped: %u frames\n", rec_n);
    }
}

static void window_shot(const char *name);
static char window_pending[32];                  /* taken in present(), before the buffer is shown */

/* ================================================================ script === */
/* --script FILE (or - for stdin): one command a line, '#' at a word's start begins a comment.
 * Time is the script's own clock (ms from power-on): commands run in order, "wait" moves the clock on.
 *   wait MS                 200 | 200ms | 1.5s;   frames N = N x 15 ms
 *   key KEY down|up         a computer key of the keyboard map, exactly as typed (A, W, F1, Z, RETURN, ESC,
 *                           R, UP, F12 ...): "key A down", "wait 300", "key A up"
 *   key KEY [MS]            press KEY for MS (default 100), the clock moves on by MS
 *   btn NAME [down|up|MS]   a panel control by name (PLAY SEL FX ... OCT- OCT+, D4 F#3 .. G5): a tap of MS
 *                           (default 100 ms; the clock moves on), or down / up
 *   knob NAME +-N           turn a knob N detents (SELECT ALGORITHM PRESETS KNOB1..KNOB4; MASTER: N x 16 ADC)
 *   master ADC              set the MASTER pot (0..1023)
 *   shot NAME|PATH          the LCD: build/emu/NAME.png (+ .ppm headless), or PATH (.ppm or .png)
 *   expect led NAME on|dim|off    check a key's / button's LED now (on = lit; GREEN: PLAY's green LED);
 *                           a failure: exit status 1
 *   expect sound|silence    non-zero samples since the previous "expect sound|silence" (or power-on)
 *   dump | rec | window NAME | quit       F12's print, F11's LCD recording, a window shot, stop here
 * Also accepted (older scripts): press KEY [MS], tap, hold / down, release / up, turn KNOB N, and a leading
 * "<ms>" for an absolute time ("1200 down A"). Names: a computer key wins for key/press/hold, a panel control
 * for btn; "note:F3" names the note F3. */
enum { OP_DOWN, OP_UP, OP_TURN, OP_SHOT, OP_WINDOW, OP_DUMP, OP_REC, OP_QUIT, OP_MASTER, OP_EXPECT_LED, OP_EXPECT_SND };
typedef struct { uint32_t ms, seq; int op, kind, idx, n; const keymap_t *km; char name[96]; } sev_t;
static sev_t *sev;
static int sev_n, sev_cap, sev_i, quit_req;
static int headless;
static int expect_ok, expect_fail;
static uint64_t expect_nz_mark;

static int find_control(const char *s, int *kind, int *idx)
{
    int i;
    char n[8];
    for (i = 0; i < EMU_NKEY; i++) {
        emu_note_name(i, n);
        if (!strcasecmp(s, n)) { *kind = KM_KEY; *idx = i; return 1; }
    }
    for (i = 0; i < EMU_NB; i++)
        if (!strcasecmp(s, EMU_BTN_NAME[i])) { *kind = KM_BTN; *idx = i; return 1; }
    if (!strcasecmp(s, "SCL")) { *kind = KM_BTN; *idx = EMU_B_SEL; return 1; }
    if (!strcasecmp(s, "ALGO")) { *kind = KM_SELECT; *idx = EMU_E_ALGO; return 1; }
    for (i = 0; i < EMU_NE; i++)
        if (!strcasecmp(s, EMU_ENC_NAME[i])) { *kind = KM_SELECT; *idx = i; return 1; }
    return 0;
}
static int find_key(const char *s, const keymap_t **km, int *kind, int *idx)
{
    int i;
    for (i = 0; i < KEYMAP_N; i++)
        if (!strcasecmp(s, KEYMAP[i].cap)) {
            *km = &KEYMAP[i];
            *kind = KEYMAP[i].kind;
            *idx = KEYMAP[i].idx;
            return 1;
        }
    if (!strcasecmp(s, "SHIFT") || !strcasecmp(s, "ENTER") || !strcasecmp(s, "BACKSPACE"))
        return find_key(s[0] == 'S' || s[0] == 's' ? "RSHIFT" : s[0] == 'E' || s[0] == 'e' ? "RETURN" : "BKSP",
                        km, kind, idx);
    return 0;
}
/* a name: a keyboard key (km set) or a control (kind, idx); controls_first: the panel's names win */
static int resolve(const char *s, const keymap_t **km, int *kind, int *idx, int controls_first)
{
    *km = NULL;
    if (!strncasecmp(s, "note:", 5))
        return find_control(s + 5, kind, idx) && *kind == KM_KEY;
    if (controls_first && find_control(s, kind, idx))
        return 1;
    return find_key(s, km, kind, idx) || find_control(s, kind, idx);
}
static int is_num(const char *s)
{
    if (*s == '+' || *s == '-')
        s++;
    return *s >= '0' && *s <= '9';
}
static uint32_t parse_dur(const char *s, uint32_t def)    /* "200ms", "200", "1.5s" */
{
    char *e;
    double v;
    if (!s || !*s)
        return def;
    v = strtod(s, &e);
    if (e == s)
        return def;
    if (*e == 's' || *e == 'S')
        v *= 1000;
    return (uint32_t)(v + .5);
}
static void sev_add(uint32_t ms, int op, const keymap_t *km, int kind, int idx, int n, const char *name)
{
    sev_t *e;
    if (sev_n == sev_cap) {
        sev_cap = sev_cap ? sev_cap * 2 : 64;
        sev = realloc(sev, (size_t)sev_cap * sizeof *sev);
    }
    e = &sev[sev_n];
    memset(e, 0, sizeof *e);
    e->ms = ms;
    e->seq = (uint32_t)sev_n++;
    e->op = op;
    e->km = km;
    e->kind = kind;
    e->idx = idx;
    e->n = n;
    if (name)
        snprintf(e->name, sizeof e->name, "%s", name);
}
static int sev_cmp(const void *a, const void *b)
{
    const sev_t *x = a, *y = b;
    if (x->ms != y->ms)
        return x->ms < y->ms ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}
static int script_errors;
static void load_script(const char *path)
{
    FILE *f = strcmp(path, "-") ? fopen(path, "r") : stdin;
    char line[256];
    uint32_t cur = 0;                             /* the script's clock */
    int ln = 0;
    if (!f) {
        printf("script: cannot open %s\n", path);
        exit(2);
    }
    while (fgets(line, sizeof line, f)) {
        char *h = line, w[5][96];
        int nw, t = 0, kind = -1, idx = 0;
        const keymap_t *km = NULL;
        const char *cmd, *arg, *arg2, *arg3;
        ln++;
        while ((h = strchr(h, '#')) != NULL) {   /* a comment: '#' at a word's start (F#4 is a name) */
            if (h == line || h[-1] == ' ' || h[-1] == '\t') {
                *h = 0;
                break;
            }
            h++;
        }
        memset(w, 0, sizeof w);
        nw = sscanf(line, "%95s %95s %95s %95s %95s", w[0], w[1], w[2], w[3], w[4]);
        if (nw < 1)
            continue;
        if (w[0][0] >= '0' && w[0][0] <= '9') {  /* "<ms> command": an absolute time */
            cur = (uint32_t)strtoul(w[0], NULL, 10);
            t = 1;
        }
        cmd = w[t];
        arg = w[t + 1];
        arg2 = w[t + 2];
        arg3 = w[t + 3];
#define BAD(...) do { printf("script line %d: ", ln); printf(__VA_ARGS__); printf("\n"); script_errors++; } while (0)
        if (!strcmp(cmd, "wait")) { cur += parse_dur(arg, 0); continue; }
        if (!strcmp(cmd, "frames")) { cur += 15u * (uint32_t)atoi(arg); continue; }
        if (!strcmp(cmd, "shot")) { sev_add(cur, OP_SHOT, NULL, 0, 0, 0, arg); continue; }
        if (!strcmp(cmd, "window")) { sev_add(cur, OP_WINDOW, NULL, 0, 0, 0, arg[0] ? arg : "window"); continue; }
        if (!strcmp(cmd, "dump")) { sev_add(cur, OP_DUMP, NULL, 0, 0, 0, NULL); continue; }
        if (!strcmp(cmd, "rec")) { sev_add(cur, OP_REC, NULL, 0, 0, 0, NULL); continue; }
        if (!strcmp(cmd, "quit")) { sev_add(cur, OP_QUIT, NULL, 0, 0, 0, NULL); continue; }
        if (!strcmp(cmd, "master")) { sev_add(cur, OP_MASTER, NULL, 0, 0, atoi(arg), NULL); continue; }
        if (!strcmp(cmd, "expect")) {
            if (!strcmp(arg, "sound") || !strcmp(arg, "silence")) {
                sev_add(cur, OP_EXPECT_SND, NULL, 0, 0, !strcmp(arg, "sound"), NULL);
            } else if (!strcmp(arg, "led") && (!strcasecmp(arg2, "GREEN") ||
                       (resolve(arg2, &km, &kind, &idx, 1) && (kind == KM_KEY || kind == KM_BTN)))) {
                if (!strcasecmp(arg2, "GREEN"))
                    kind = -1;                    /* PLAY's green LED */
                int lv = !strcmp(arg3, "on") || !strcmp(arg3, "lit") ? 2 : !strcmp(arg3, "dim") ? 1
                       : !strcmp(arg3, "off") ? 0 : -1;
                if (lv < 0)
                    BAD("expect led %s: on, dim or off, not \"%s\"", arg2, arg3);
                else {
                    char d[96];
                    snprintf(d, sizeof d, "led %s %s", arg2, arg3);
                    sev_add(cur, OP_EXPECT_LED, NULL, kind, idx, lv, d);
                }
            } else {
                BAD("expect: \"sound\", \"silence\" or \"led KEY on|dim|off\" (a key or a button)");
            }
            continue;
        }
        if (!strcmp(cmd, "key") || !strcmp(cmd, "btn") || !strcmp(cmd, "press") || !strcmp(cmd, "tap") ||
            !strcmp(cmd, "hold") || !strcmp(cmd, "down") || !strcmp(cmd, "release") || !strcmp(cmd, "up")) {
            int controls = !strcmp(cmd, "btn");
            const char *mode = arg2;
            if (!arg[0] || !resolve(arg, &km, &kind, &idx, controls)) {
                BAD("unknown %s \"%s\"", controls ? "control" : "key", arg);
                continue;
            }
            if (!strcmp(cmd, "hold") || !strcmp(cmd, "down"))
                mode = "down";
            else if (!strcmp(cmd, "release") || !strcmp(cmd, "up"))
                mode = "up";
            if (!strcmp(mode, "down")) {
                sev_add(cur, OP_DOWN, km, kind, idx, 0, NULL);
            } else if (!strcmp(mode, "up")) {
                sev_add(cur, OP_UP, km, kind, idx, 0, NULL);
            } else {                              /* a press of MS */
                uint32_t d = parse_dur(mode, !strcmp(cmd, "tap") ? 60u : 100u);
                sev_add(cur, OP_DOWN, km, kind, idx, 0, NULL);
                sev_add(cur + d, OP_UP, km, kind, idx, 0, NULL);
                if (!t)
                    cur += d;
            }
            continue;
        }
        if (!strcmp(cmd, "knob") || !strcmp(cmd, "turn")) {
            if (!arg[0] || !resolve(arg, &km, &kind, &idx, 1) || kind != KM_SELECT) {
                BAD("\"%s\" is not a knob (SELECT ALGORITHM PRESETS KNOB1..KNOB4 MASTER)", arg);
                continue;
            }
            if (arg2[0] && !is_num(arg2)) {
                BAD("knob %s: a number of detents, not \"%s\"", arg, arg2);
                continue;
            }
            sev_add(cur, OP_TURN, NULL, kind, idx, arg2[0] ? atoi(arg2) : 1, NULL);
            continue;
        }
        BAD("unknown command \"%s\"", cmd);
#undef BAD
    }
    if (f != stdin)
        fclose(f);
    qsort(sev, (size_t)sev_n, sizeof *sev, sev_cmp);
}

/* what a key of the keyboard map does (keyboard, script) */
static void key_action(const keymap_t *m, int down, int src)
{
    switch (m->kind) {
    case KM_KEY: hold_key(m->idx, src, down); break;
    case KM_BTN: hold_btn(m->idx, src, down); break;
    case KM_OCTBOTH:
        hold_btn(EMU_B_OCTDN, src == SRC_KEY ? SRC_ESC : src, down);
        hold_btn(EMU_B_OCTUP, src == SRC_KEY ? SRC_ESC : src, down);
        break;
    case KM_SELECT: if (down) sel_knob = m->idx; break;
    case KM_TURN: if (down) turn(sel_knob, sel_knob == EMU_E_MASTER ? 2 * m->idx : m->idx); break;
    case KM_SHOT: if (down) shot(NULL); break;
    case KM_RECORD: if (down) toggle_record(); break;
    case KM_DUMP: if (down) emu_fw_dump(); break;
    }
}

static void run_script(uint32_t ms)
{
    while (sev_i < sev_n && sev[sev_i].ms <= ms) {
        sev_t *e = &sev[sev_i++];
        switch (e->op) {
        case OP_DOWN:
        case OP_UP:
            if (e->km)
                key_action(e->km, e->op == OP_DOWN, SRC_SCRIPT);
            else if (e->kind == KM_KEY)
                hold_key(e->idx, SRC_SCRIPT, e->op == OP_DOWN);
            else if (e->kind == KM_BTN)
                hold_btn(e->idx, SRC_SCRIPT, e->op == OP_DOWN);
            else if (e->kind == KM_SELECT && e->op == OP_DOWN)
                sel_knob = e->idx;
            break;
        case OP_TURN: turn(e->idx, e->n); break;
        case OP_MASTER: emu_hal.master = e->n; break;
        case OP_SHOT: shot(e->name[0] ? e->name : NULL); break;
        case OP_WINDOW:
            if (headless)
                printf("script: no window in --headless\n");
            else
                snprintf(window_pending, sizeof window_pending, "%s", e->name);
            break;
        case OP_DUMP: emu_fw_dump(); break;
        case OP_REC: toggle_record(); break;
        case OP_EXPECT_LED: {
            uint8_t q = emu_hal.led_play_green;
            int lv = e->kind < 0 ? 2 * ((emu_hal.led[q >> 3] >> (q & 7)) & 1)
                   : led_state(e->kind == KM_KEY ? 14 + e->idx : emu_hal.btn_id[e->idx]);
            int ok = lv == e->n;
            printf("expect %s at %u ms: %s (the LED is %s)\n", e->name, (unsigned)ms, ok ? "ok" : "FAILED",
                   lv == 2 ? "lit" : lv == 1 ? "dim" : "off");
            if (ok) expect_ok++; else expect_fail++;
            break;
        }
        case OP_EXPECT_SND: {
            uint64_t nz = ast.nz_samples - expect_nz_mark;
            int ok = e->n ? nz > 0 : nz == 0;
            printf("expect %s at %u ms: %s (%llu non-zero samples since the last check)\n", e->n ? "sound" : "silence",
                   (unsigned)ms, ok ? "ok" : "FAILED", (unsigned long long)nz);
            expect_nz_mark = ast.nz_samples;
            if (ok) expect_ok++; else expect_fail++;
            break;
        }
        case OP_QUIT: quit_req = 1; break;
        }
    }
}

/* ================================================================= bench === */
/* --bench SECS: no window, no audio device; simulated time: the timer every ms, a UI frame every 15 ms,
 * the audio blocks as they fall due. Measures the render; --wav writes the output. */
static void wav_header(FILE *f, uint32_t frames)
{
    uint32_t v;
    uint16_t s;
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); v = 36 + frames * 4; fwrite(&v, 4, 1, f);
    fwrite("WAVEfmt ", 1, 8, f); v = 16; fwrite(&v, 4, 1, f);
    s = 1; fwrite(&s, 2, 1, f); s = 2; fwrite(&s, 2, 1, f);
    v = EMU_FS; fwrite(&v, 4, 1, f); v = EMU_FS * 4; fwrite(&v, 4, 1, f);
    s = 4; fwrite(&s, 2, 1, f); s = 16; fwrite(&s, 2, 1, f);
    fwrite("data", 1, 4, f); v = frames * 4; fwrite(&v, 4, 1, f);
}
static int bench(void)
{
    uint32_t ms, end = (uint32_t)lround(opt_bench * 1000), last_frame = 0;
    uint64_t frames_done = 0;
    int16_t blk[EMU_BLOCK * 2];
    double peak = 0, sumsq = 0;
    for (ms = 0; ms <= end && !quit_req; ms++) {
        emu_fw_tick(ms);
        run_script(ms);
        if (ms == 0 || ms - last_frame >= 15u) {
            last_frame = ms;
            ui_frame(ms * 1000.0);
            if (recording)
                record_frame();
        } else {
            emu_fw_idle();
        }
        while (frames_done + EMU_BLOCK <= (uint64_t)ms * EMU_FS / 1000u) {
            int i;
            render_block(blk);
            for (i = 0; i < EMU_BLOCK * 2; i++) {
                double v = blk[i] / 32768.0;
                sumsq += v * v;
                if (fabs(v) > peak)
                    peak = fabs(v);
            }
            if (wav) {
                fwrite(blk, 4, EMU_BLOCK, wav);
                wav_frames += EMU_BLOCK;
            }
            frames_done += EMU_BLOCK;
        }
    }
    if (sev_i < sev_n)
        printf("headless: %d script command(s) after %u ms not run (--frames too small)\n", sev_n - sev_i, end);
    printf("headless: %.1f s simulated, output peak %.3f, rms %.4f\n", ms / 1000.0, peak,
           frames_done ? sqrt(sumsq / (double)(frames_done * 2)) : 0.0);
    return 0;
}

/* ================================================================== main === */
static void usage(void)
{
    printf("FM-1 emulator: the firmware with its LCD, LEDs, audio and MIDI.\n"
           "usage: build/host/emu [options]\n"
           "  --headless       no window, no audio device (SDL dummy drivers), simulated time: deterministic.\n"
           "                   Runs to the end of the script (else 600 frames); shots also as PPM\n"
           "  --frames N       with --headless: run N UI frames of 15 ms\n"
           "  --script FILE    input from a file (- = stdin), one command a line (tools/emu/README.md):\n"
           "                     key A down | key A up | key A 300 | wait 200 | btn PLAY | btn SEL down |\n"
           "                     knob KNOB1 +3 | master ADC | shot NAME | expect led A on | expect sound |\n"
           "                     dump | quit\n"
           "  --shot PATH      at the end, write the LCD to PATH (.ppm: PPM, else PNG)\n"
           "  --wav FILE       with --headless: write the audio\n"
           "  --demo           load 4 patterns (ACID, PAD, LEAD, BEAT) and press PLAY (Felucca; ChoralRoot: none)\n"
           "  --flash PATH     the flash image file (default build/emu/flash.bin; headless: none unless given):\n"
           "                   settings, user sounds and loops persist in it\n"
           "  --no-flash       RAM-only flash, erased at power-on\n"
           "  --save-on-exit   save the settings at exit (they also save 1.5 s after a change)\n"
           "  --scale N        LCD scale in the window (default 3, smaller if the screen is too small)\n"
           "  --front          a normal app: Dock icon, the window centred and in front. Default: a background\n"
           "                   app (SDL_HINT_MAC_BACKGROUND_APP), no Dock icon, never takes the focus, its\n"
           "                   window at the bottom-right of the main display; click it to play\n"
           "  --quit-after S   quit after S seconds (prints the timing)\n"
           "  --midi-log       print every MIDI event in and out\n"
           "  --no-midi        no CoreMIDI ports\n"
           "  --no-audio       no audio device (the firmware still runs; no sound)\n"
           "exit status: 0, 1 if an \"expect\" failed, 2 on a script error\n\n");
    keymap_help();
}

static SDL_Window *win;
static SDL_Renderer *ren;
static SDL_Texture *t_lcd, *t_lcd_small, *t_panel;
static int win_w, win_h, lcd_px, pix_w, pix_h;
static float dpi = 1;
static uint16_t lcd_rgb[EMU_LCD_W * EMU_LCD_H];
static uint32_t lcd_seen = 0xFFFFFFFFu;
static uint8_t led_seen[2 * EMU_NCOL];
static uint32_t panel_sig, panel_seen = 1;
static int timer_run = 1;
static uint32_t stall_max;                       /* the main loop's longest pass (ms) */
static uint32_t t0_ms;

static int timer_thread(void *u)
{
    uint32_t last = 0xFFFFFFFFu;
    (void)u;
    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);   /* an ISR */
    while (__atomic_load_n(&timer_run, __ATOMIC_ACQUIRE)) {
        uint32_t ms = SDL_GetTicks() - t0_ms, pkt;
        if (ms != last) {
            last = ms;
            emu_fw_tick(ms);
            while (emu_midi_take(&pkt))                /* usb_poll: EP1 OUT -> midi_in_event */
                if (!emu_fw_midi_in(pkt)) {
                    emu_midi_unget(pkt);
                    break;
                }
            while (emu_fw_midi_out_take(&pkt))         /* ep1_tx */
                emu_midi_send(pkt);
        }
        SDL_Delay(1);
    }
    return 0;
}

static void update_title(void)
{
    static int s_sel = -1, s_m = -1, s_r = -1;
    char t[160];
    if (s_sel == sel_knob && s_m == emu_hal.master && s_r == recording)
        return;
    s_sel = sel_knob;
    s_m = emu_hal.master;
    s_r = recording;
    snprintf(t, sizeof t, "FM-1 emulator  |  Up/Down: %s  |  MASTER %d%%%s", EMU_ENC_NAME[sel_knob],
             emu_hal.master * 100 / 1023, recording ? "  |  REC LCD" : "");
    SDL_SetWindowTitle(win, t);
}

static void present(void)
{
    uint32_t sig = 0;
    int i;
    SDL_Rect r;
    if (emu_hal.lcd_writes != lcd_seen) {
        lcd_seen = emu_hal.lcd_writes;
        for (i = 0; i < EMU_LCD_W * EMU_LCD_H; i++)
            lcd_rgb[i] = (uint16_t)((emu_hal.lcd[i] >> 8) | (emu_hal.lcd[i] << 8));
        SDL_UpdateTexture(t_lcd, NULL, lcd_rgb, EMU_LCD_W * 2);
        SDL_UpdateTexture(t_lcd_small, NULL, lcd_rgb, EMU_LCD_W * 2);
        if (recording)
            record_frame();
    }
    for (i = 0; i < EMU_NKEY; i++)
        sig = sig * 31u + key_src[i];
    for (i = 0; i < EMU_NB; i++)
        sig = sig * 31u + btn_src[i];
    for (i = 0; i < EMU_NE; i++)
        sig = sig * 31u + (uint32_t)(int)(knob_angle[i] * 100);
    sig = sig * 31u + (uint32_t)sel_knob;
    sig = sig * 31u + (uint32_t)emu_hal.master;
    if (sig != panel_sig || memcmp(led_seen, emu_hal.led, EMU_NCOL) || memcmp(led_seen + EMU_NCOL, emu_hal.led_dim, EMU_NCOL) ||
        panel_seen) {
        panel_sig = sig;
        panel_seen = 0;
        memcpy(led_seen, emu_hal.led, EMU_NCOL);
        memcpy(led_seen + EMU_NCOL, emu_hal.led_dim, EMU_NCOL);
        draw_panel();
        SDL_UpdateTexture(t_panel, NULL, cv.p, cv.w * 4);
    }
    SDL_SetRenderDrawColor(ren, 14, 14, 16, 255);
    SDL_RenderClear(ren);
    r = (SDL_Rect){0, 0, lcd_px, lcd_px};
    SDL_RenderCopy(ren, t_lcd, NULL, &r);
    r = (SDL_Rect){0, lcd_px, pix_w, pix_h};
    SDL_RenderCopy(ren, t_panel, NULL, &r);
    r = (SDL_Rect){(int)lroundf((SCREEN_A.x - VIEW.x) * cv.k), lcd_px + (int)lroundf((SCREEN_A.y - VIEW.y) * cv.k),
                   (int)lroundf(SCREEN_A.w * cv.k), (int)lroundf(SCREEN_A.h * cv.k)};
    SDL_RenderCopy(ren, t_lcd_small, NULL, &r);
    if (window_pending[0]) {
        window_shot(window_pending);
        window_pending[0] = 0;
    }
    SDL_RenderPresent(ren);
    update_title();
}

static void window_shot(const char *name)
{
    char p[1200];
    int w, h;
    uint32_t *px;
    if (!ren)
        return;
    SDL_GetRendererOutputSize(ren, &w, &h);
    px = malloc((size_t)w * (size_t)h * 4u);
    if (SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ARGB8888, px, w * 4) == 0) {
        snprintf(p, sizeof p, "%s%s.png", out_dir, name);
        if (emu_write_png_argb(p, px, w, h) == 0)
            printf("window shot: %s\n", p);
    }
    free(px);
}

/* window (logical) point -> drawing units; 0 if on the LCD */
static int to_units(int x, int y, float *ux, float *uy)
{
    float px = x * dpi, py = y * dpi - lcd_px;
    if (py < 0)
        return 0;
    *ux = px / cv.k + VIEW.x;
    *uy = py / cv.k + VIEW.y;
    return 1;
}
static int hit(float ux, float uy, int *kind, int *idx)
{
    int i;
    for (i = 0; i < EMU_NKEY; i++)
        if (ux >= KEYR[i].x && ux < KEYR[i].x + KEYR[i].w && uy >= KEYR[i].y && uy < KEYR[i].y + KEYR[i].h) {
            *kind = KM_KEY; *idx = i; return 1;
        }
    for (i = 0; i < EMU_NB; i++)
        if (ux >= BTNR[i].x && ux < BTNR[i].x + BTNR[i].w && uy >= BTNR[i].y && uy < BTNR[i].y + BTNR[i].h) {
            *kind = KM_BTN; *idx = i; return 1;
        }
    for (i = 0; i < EMU_NE; i++) {
        float dx = ux - ENC_CX[i], dy = uy - ENC_CY[i];
        if (dx * dx + dy * dy <= (ENC_R + 5) * (ENC_R + 5)) {
            *kind = KM_SELECT; *idx = i; return 1;
        }
    }
    return 0;
}

static int mouse_kind = -1, mouse_idx, drag_knob = -1;
static float drag_acc, wheel_acc;
static int drag_y;

static void key_event(const SDL_KeyboardEvent *e, int down)
{
    const keymap_t *m = keymap_find(e->keysym.scancode);
    if (!m || (down && (e->keysym.mod & KMOD_GUI)))
        return;                                   /* (Cmd shortcuts are the system's) */
    if (e->repeat && m->kind != KM_TURN)
        return;
    key_action(m, down, SRC_KEY);
}

static void mouse_event(const SDL_Event *e)
{
    float ux, uy;
    int kind, idx;
    if (e->type == SDL_MOUSEBUTTONDOWN) {
        if (!to_units(e->button.x, e->button.y, &ux, &uy) || !hit(ux, uy, &kind, &idx))
            return;
        if (kind == KM_SELECT) {
            sel_knob = idx;
            drag_knob = idx;
            drag_y = e->button.y;
            drag_acc = 0;
            return;
        }
        if (e->button.button == SDL_BUTTON_RIGHT) {   /* latch */
            uint8_t on = kind == KM_KEY ? key_src[idx] & SRC_LATCH : btn_src[idx] & SRC_LATCH;
            if (kind == KM_KEY) hold_key(idx, SRC_LATCH, !on);
            else hold_btn(idx, SRC_LATCH, !on);
            return;
        }
        mouse_kind = kind;
        mouse_idx = idx;
        if (kind == KM_KEY) hold_key(idx, SRC_MOUSE, 1);
        else hold_btn(idx, SRC_MOUSE, 1);
    } else if (e->type == SDL_MOUSEBUTTONUP) {
        if (e->button.button == SDL_BUTTON_RIGHT)
            return;
        drag_knob = -1;
        if (mouse_kind == KM_KEY) hold_key(mouse_idx, SRC_MOUSE, 0);
        else if (mouse_kind == KM_BTN) hold_btn(mouse_idx, SRC_MOUSE, 0);
        mouse_kind = -1;
    } else if (e->type == SDL_MOUSEMOTION && drag_knob >= 0) {
        int s;
        drag_acc += (float)(drag_y - e->motion.y) / 8.f;   /* 8 points per detent, up = clockwise */
        drag_y = e->motion.y;
        s = (int)drag_acc;
        drag_acc -= (float)s;
        turn(drag_knob, s);
    } else if (e->type == SDL_MOUSEWHEEL) {
        int mx, my, s;
        float dy = e->wheel.preciseY;
        if (e->wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
            dy = -dy;
        SDL_GetMouseState(&mx, &my);
        if (!to_units(mx, my, &ux, &uy) || !hit(ux, uy, &kind, &idx) || kind != KM_SELECT)
            return;
        wheel_acc += dy;
        s = (int)wheel_acc;
        wheel_acc -= (float)s;
        turn(idx, s);
    }
}

static void make_dirs(void)
{
    char *b = SDL_GetBasePath();                  /* build/host/ */
    char p[1100];
    char r[PATH_MAX];
    snprintf(p, sizeof p, "%s../emu", b ? b : "./");
    SDL_free(b);
    mkdir(p, 0755);
    snprintf(out_dir, sizeof out_dir, "%s/", realpath(p, r) ? r : p);
}

int main(int argc, char **argv)
{
    int i, running = 1;
    for (i = 1; i < argc; i++) {
        const char *a = argv[i], *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) { usage(); return 0; }
        else if (!strcmp(a, "--scale") && v) { opt_scale = atoi(v); i++; }
        else if (!strcmp(a, "--demo")) opt_demo = 1;
        else if (!strcmp(a, "--flash") && v) { opt_flash = v; i++; }
        else if (!strcmp(a, "--no-flash")) opt_no_flash = 1;
        else if (!strcmp(a, "--save-on-exit")) opt_save_on_exit = 1;
        else if (!strcmp(a, "--script") && v) { opt_script = v; i++; }
        else if (!strcmp(a, "--quit-after") && v) { opt_quit_after = atof(v); i++; }
        else if (!strcmp(a, "--bench") && v) { opt_bench = atof(v); i++; }
        else if (!strcmp(a, "--headless")) {
            headless = 1;
            if (v && v[0] >= '0' && v[0] <= '9') { opt_bench = atoi(v) * 0.015; i++; }
        }
        else if (!strcmp(a, "--frames") && v) { opt_frames = atoi(v); i++; }
        else if (!strcmp(a, "--shot") && v) { opt_shot = v; i++; }
        else if (!strcmp(a, "--front")) opt_front = 1;
        else if (!strcmp(a, "--wav") && v) { opt_wav = v; i++; }
        else if (!strcmp(a, "--midi-log")) opt_midi_log = 1;
        else if (!strcmp(a, "--no-midi")) opt_midi = 0;
        else if (!strcmp(a, "--no-audio")) opt_audio = 0;
        else { printf("unknown option %s (--help)\n", a); return 1; }
    }
    setvbuf(stdout, NULL, _IOLBF, 0);
    geometry_init();
    for (i = 0; i < 41; i++)
        led_pos[i] = 0xFF;
    {
        int p, r;
        for (p = 0; p < EMU_NCOL; p++)
            for (r = 1; r < 5; r++)
                if (emu_keymap[r][p] >= 0)
                    led_pos[emu_keymap[r][p]] = (uint8_t)(p << 3 | r);
    }
    if (opt_script)
        load_script(opt_script);

    if (script_errors) {
        printf("script: %d error(s) in %s\n", script_errors, opt_script);
        return 2;
    }
    if (opt_frames > 0) {                         /* (implies --headless: never a window) */
        headless = 1;
        opt_bench = opt_frames * 0.015;
    }
    if (opt_bench > 0)
        headless = 1;
    if (headless && opt_bench <= 0)              /* to the end of the script, else 600 frames */
        opt_bench = sev_n ? (sev[sev_n - 1].ms + 15u) / 1000.0 : 9.0;
    emu_fw_options(opt_flash, opt_no_flash, opt_save_on_exit, headless);
    if (headless) {                               /* no window, no audio device: simulated time */
        /* nothing here opens a window or an audio device (SDL_Init(0): no subsystem); the dummy drivers
         * make sure of it should a subsystem ever be added */
        setenv("SDL_VIDEODRIVER", "dummy", 1);
        setenv("SDL_AUDIODRIVER", "dummy", 1);
        shot_ppm = 1;
        if (SDL_Init(0) != 0)
            return 1;
        make_dirs();
        emu_hal.ready = 2;                        /* simulated time */
        emu_fw_init(opt_demo);
        if (opt_wav) {
            wav = fopen(opt_wav, "wb");
            if (wav)
                wav_header(wav, 0);
        }
        bench();
        if (wav) {
            wav_header(wav, wav_frames);
            fclose(wav);
            printf("headless: wrote %s\n", opt_wav);
        }
        if (opt_shot)
            write_lcd(opt_shot);
        print_stats();
        SDL_Quit();
        if (expect_ok + expect_fail)
            printf("expect: %d ok, %d failed\n", expect_ok, expect_fail);
        return expect_fail ? 1 : 0;
    }

    if (!opt_front) {                             /* no Dock icon, no activation: never steals the focus */
        SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "1");
        SDL_SetHint(SDL_HINT_WINDOW_NO_ACTIVATION_WHEN_SHOWN, "1");
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    SDL_SetHint(SDL_HINT_MAC_CTRL_CLICK_EMULATE_RIGHT_CLICK, "1");
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) {
        printf("SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    make_dirs();
    {   /* the window: LCD x scale, the panel below at the same width */
        SDL_Rect ub;
        int s = opt_scale > 0 ? opt_scale : 3;
        if (!opt_scale && SDL_GetDisplayUsableBounds(0, &ub) == 0)
            while (s > 1 && 240 * s + (int)(240 * s * VIEW.h / VIEW.w) > ub.h - 28)
                s--;
        win_w = 240 * s;
        win_h = win_w + (int)lroundf(win_w * VIEW.h / VIEW.w);
        {   /* bottom-right of the screen (it never covers the centre), centred with --front */
            int x = SDL_WINDOWPOS_CENTERED, y = SDL_WINDOWPOS_CENTERED;
            if (!opt_front && SDL_GetDisplayUsableBounds(0, &ub) == 0) {
                x = ub.x + ub.w - win_w - 8;
                y = ub.y + ub.h - win_h - 8;
                if (y < ub.y + 28)
                    y = ub.y + 28;
            }
            win = SDL_CreateWindow("FM-1 emulator", x, y, win_w, win_h, SDL_WINDOW_ALLOW_HIGHDPI);
        }
        if (!win) {
            printf("SDL_CreateWindow: %s\n", SDL_GetError());
            return 1;
        }
        ren = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
        if (!ren)
            ren = SDL_CreateRenderer(win, -1, 0);
        {
            int ow, oh;
            SDL_GetRendererOutputSize(ren, &ow, &oh);
            dpi = (float)ow / (float)win_w;
        }
        lcd_px = (int)lroundf(win_w * dpi);
        pix_w = lcd_px;
        pix_h = (int)lroundf(win_w * dpi * VIEW.h / VIEW.w);
        base = (canvas_t){calloc((size_t)pix_w * (size_t)pix_h, 4), pix_w, pix_h, (float)pix_w / VIEW.w, VIEW.x, VIEW.y};
        cv = base;
        cv.p = calloc((size_t)pix_w * (size_t)pix_h, 4);
        t_lcd = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, EMU_LCD_W, EMU_LCD_H);
        t_lcd_small = SDL_CreateTexture(ren, SDL_PIXELFORMAT_RGB565, SDL_TEXTUREACCESS_STREAMING, EMU_LCD_W, EMU_LCD_H);
        t_panel = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, pix_w, pix_h);
        SDL_SetTextureScaleMode(t_lcd, SDL_ScaleModeNearest);
        SDL_SetTextureScaleMode(t_lcd_small, SDL_ScaleModeLinear);
        printf("window %dx%d points (LCD x%d, %.0fx pixels)\n", win_w, win_h, s, dpi);
    }
    emu_fw_init(opt_demo);
    draw_base();
    t0_ms = SDL_GetTicks();
    emu_fw_tick(0);
    if (opt_midi && emu_midi_open(opt_midi_log) != 0)
        printf("MIDI: TODO, CoreMIDI unavailable here\n");
    {
        SDL_Thread *th = SDL_CreateThread(timer_thread, "fm1-timer", NULL);
        SDL_AudioDeviceID dev = 0;
        uint32_t last_frame = 0, last_idle = 0, loop_ms = 0;
        if (opt_audio) {
            SDL_AudioSpec want, have;
            SDL_zero(want);
            want.freq = EMU_FS;
            want.format = AUDIO_S16SYS;
            want.channels = 2;
            want.samples = EMU_BLOCK;
            want.callback = audio_cb;
            dev = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
            if (!dev)
                printf("audio: %s (running without sound)\n", SDL_GetError());
            else {
                printf("audio: %d Hz, %d ch, %d-frame device buffer, %d-frame firmware blocks\n", have.freq,
                       have.channels, have.samples, EMU_BLOCK);
                {   /* warm the render (code, tables, stacks) before the device's deadlines count */
                    int16_t tmp[EMU_BLOCK * 2];
                    for (i = 0; i < 8; i++)
                        emu_fw_audio(tmp, EMU_BLOCK);
                }
                SDL_PauseAudioDevice(dev, 0);
            }
        }
        ui_frame(perf_us());
        {
            double tp = perf_us();
            present();
            if (getenv("EMU_TRACE"))
                printf("trace: first present %.0f us\n", perf_us() - tp);
        }
        while (running && !quit_req) {
            SDL_Event e;
            uint32_t ms;
            while (SDL_PollEvent(&e)) {
                switch (e.type) {
                case SDL_QUIT: running = 0; break;
                case SDL_KEYDOWN: key_event(&e.key, 1); break;
                case SDL_KEYUP: key_event(&e.key, 0); break;
                case SDL_MOUSEBUTTONDOWN: case SDL_MOUSEBUTTONUP: case SDL_MOUSEMOTION: case SDL_MOUSEWHEEL:
                    mouse_event(&e);
                    break;
                case SDL_WINDOWEVENT:
                    if (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                        release_src(SRC_KEY | SRC_ESC | SRC_MOUSE);    /* no stuck notes */
                    else if (e.window.event == SDL_WINDOWEVENT_EXPOSED)
                        panel_seen = 1;
                    break;
                }
            }
            ms = SDL_GetTicks() - t0_ms;
            if (ms - loop_ms > 100u && getenv("EMU_TRACE"))
                printf("trace: main loop stalled %u ms at %u ms\n", ms - loop_ms, ms);
            if (ms - loop_ms > stall_max)
                stall_max = ms - loop_ms;
            loop_ms = ms;
            run_script(ms);
            if (opt_quit_after > 0 && ms >= opt_quit_after * 1000)
                running = 0;
            if (ms - last_frame >= 15u) {             /* main.c: a UI frame at most every 15 ms */
                last_frame = ms;
                ui_frame(perf_us());
                present();
            } else {
                if (ms != last_idle) {
                    last_idle = ms;
                    emu_fw_idle();
                }
                SDL_Delay(1);
            }
        }
        if (dev)
            SDL_CloseAudioDevice(dev);
        __atomic_store_n(&timer_run, 0, __ATOMIC_RELEASE);
        SDL_WaitThread(th, NULL);
    }
    emu_midi_close();
    if (opt_shot)
        write_lcd(opt_shot);
    print_stats();
    printf("ui: the main loop's longest pass %u ms\n", stall_max);
    SDL_DestroyRenderer(ren);
    SDL_DestroyWindow(win);
    SDL_Quit();
    if (expect_ok + expect_fail)
        printf("expect: %d ok, %d failed\n", expect_ok, expect_fail);
    return expect_fail ? 1 : 0;
}
