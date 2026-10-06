/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: naming a sound with the root keys (mock-up state 23), Felucca's ui_name.c idea re-done small.
 * Upper case, at most 12 characters, the cursor at the end (blinking):
 *   white roots D4..G5  phone style: ABC DEF GHI JKL MNO PQRS TUV WXYZ 0123 4567 89-. ; a tap types the group's
 *                       first character, another tap of the same key within 0.8 s the next one (cycling)
 *   black roots         D#4 a space (the others: nothing)
 *   OCT-                deletes the last character;  KNOB 2: the last character through " A..Z 0..9 -."
 * The name starts as the sound's own, "pristine" (grey): the first letter typed replaces it, OCT- edits it.
 * cr_ui.c opens it on SAVE and owns OCT+ (save), SAVE (cancel) and KNOB 1 (the slot). */
#define CN_LEN 12u
#define CN_TAP_MS 800u
static const char *const CN_GROUP[11] = {"ABC", "DEF", "GHI", "JKL", "MNO", "PQRS", "TUV", "WXYZ", "0123",
                                         "4567", "89-."};
static const char CN_SET[] = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-.";
static struct {
    uint8_t len, key, tap, pristine;   /* key: the white root (place + 1) being cycled, 0 none */
    uint32_t t;
    char s[CN_LEN + 1u];
} cn;

static void cn_open(const char *prefill)
{
    str_cpy(cn.s, prefill, sizeof cn.s);
    cn.len = (uint8_t)str_len(cn.s);
    cn.key = cn.tap = 0;
    cn.pristine = 1;
}
static void cn_touch(void)                         /* the first edit of the prefilled name */
{
    cn.pristine = 0;
}
static void cn_put(char c)
{
    if (cn.len < CN_LEN) {
        cn.s[cn.len++] = c;
        cn.s[cn.len] = 0;
    }
}
static void cn_white(uint32_t place, uint32_t now) /* white root `place` (0 = D4) */
{
    const char *g = CN_GROUP[place % 11u];
    if (cn.pristine) {
        cn.len = 0;
        cn.s[0] = 0;
        cn_touch();
    }
    if (cn.key == place + 1u && now - cn.t < CN_TAP_MS && cn.len) {
        cn.tap = (uint8_t)((cn.tap + 1u) % str_len(g));
        cn.s[cn.len - 1u] = g[cn.tap];
    } else {
        cn.key = (uint8_t)(place + 1u);
        cn.tap = 0;
        cn_put(g[0]);
    }
    cn.t = now;
}
static void cn_space(void)
{
    if (cn.pristine)
        cn_touch();
    cn.key = 0;
    cn_put(' ');
}
static void cn_delete(void)
{
    cn_touch();
    cn.key = 0;
    if (cn.len)
        cn.s[--cn.len] = 0;
}
static void cn_knob(int32_t s)                     /* KNOB 2: the last character (none: a new one) */
{
    uint32_t n = sizeof CN_SET - 1u, i = 0;
    cn_touch();
    cn.key = 0;
    if (!cn.len)
        cn_put('A');
    else {
        while (i < n && CN_SET[i] != cn.s[cn.len - 1u])
            i++;
        cn.s[cn.len - 1u] = CN_SET[(i + n + (uint32_t)(s % (int32_t)n + (int32_t)n)) % n];
    }
}
/* the name as it is saved: no spaces at either end (b holds 13); "" if empty */
static void cn_result(char *b)
{
    uint32_t i = 0, e = cn.len;
    while (i < e && cn.s[i] == ' ')
        i++;
    while (e > i && cn.s[e - 1u] == ' ')
        e--;
    str_cpy(b, cn.s + i, e - i + 1u);
}
/* the line shown: the name and the blinking cursor */
static void cn_line(char *b, uint32_t n, uint32_t now)
{
    uint32_t l;
    str_cpy(b, cn.s, n);
    l = str_len(b);
    if (l + 2u <= n && ((now / 400u) & 1u) == 0u) {
        b[l] = '_';
        b[l + 1u] = 0;
    }
}
