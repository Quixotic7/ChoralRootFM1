/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the boot stub's decision (firmware/stub/stub_core.h; the device part is stub.c, the loader's
 * interruption model is tests/ldr_test.c): the CRC-32 against a bit-by-bit reference, the marker (good, bad magic,
 * bad CRC, torn, truncated length, erased sector), the rescue key's rule (the tail of the window: early garbage,
 * bounce, both keys, a stuck row), the app's requests through the rescue word, the staged loader (good, erased, cut off,
 * damaged heads, not ours), the rescue word (0, 1, garbage), the record against ota.c's, the mailbox words. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int plain_calls;
#define STUB_PLAIN_WINDOW() (plain_calls++)
#include "../firmware/stub/stub_core.h"

static int fails, checks;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                                                 printf(__VA_ARGS__); printf("\n"); } } while (0)

static uint32_t crc32_ref(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, i;
    while (n--) {
        c ^= *p++;
        for (i = 0; i < 8u; i++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}

static uint32_t rnd_s = 12345u;
static uint32_t rnd(void) { rnd_s = rnd_s * 1664525u + 1013904223u; return rnd_s >> 8; }

/* ---- a flash model: the three plain views the stub reads ---- */
#define BODY_LEN 40000u
static uint32_t body[STUB_BODY_MAX / 4u];
static uint8_t mark[32], ldr[STUB_LDR_LEN];
static stub_crc_t K;

static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void make_marker(uint32_t len)
{
    wr32(mark, STUB_MARK_MAGIC);
    wr32(mark + 4, STUB_MARK_FORMAT);
    wr32(mark + 8, len);
    wr32(mark + 12, crc32_ref((const uint8_t *)body, len));
    wr32(mark + 16, crc32_ref(mark, 16));
    memset(mark + 20, 0x5A, 12);                     /* the compensation bytes: not the stub's business */
}

/* a staged loader as ota.c ota_stage leaves it: ota_stage_head's head, then build.py ldr_wrap's inner head and
 * blocks (the image: n bytes in 4096-byte blocks, here stored with made-up compressed lengths) */
static uint32_t make_loader(uint32_t image)
{
    uint8_t *h = ldr, *in = ldr + 32;
    uint32_t p = 0x40, left = image, k = 0;
    memset(ldr, 0xFF, sizeof ldr);
    while (left) {
        uint32_t d = left > 4096u ? 4096u : left, c = 700u + 13u * k;
        wr32(ldr + p, c);
        wr32(ldr + p + 4, d);
        memset(ldr + p + 8, (int)(0x30u + k), c);
        if (!k)
            memcpy(ldr + 0x4C, "FELUCCA-LOADER-1", 16);   /* the first block's literals (crt0_ldr.S) */
        p += 8u + c;
        left -= d;
        k++;
    }
    memset(in, 0, 32);
    wr32(in + 4, image);
    wr32(in + 8, 0x01C0A800u);
    memcpy(in + 16, "usb_hid_ota.bin", 16);
    stub_wr16(in, stub_crc16(in + 2, 30));
    memset(h, 0, 32);
    stub_wr16(h + 2, 0xFFFFu);
    wr32(h + 4, STUB_LDR_OFF);
    wr32(h + 8, p);                                  /* len + 32, len = ota.bin's body from its byte 0x20 */
    h[12] = 0x41;
    h[13] = 0xFF;
    stub_wr16(h + 14, 0xFFFFu);
    memcpy(h + 16, "LOADER.BIN", 11);
    stub_wr16(h, stub_crc16(h + 2, 30));
    return p;
}

static stub_result_t run(uint32_t key, uint32_t word)
{
    static stub_crc_t k;
    stub_view_t v;
    stub_result_t r;
    v.mark = mark;
    v.body = body;
    v.ldr = ldr;
    memset(&r, 0xEE, sizeof r);
    plain_calls = 0;
    stub_decide(&v, &k, key, word, &r);
    return r;
}

/* ---- ota.c's record, for the byte comparison ---- */
#define FELUCCA_ID "FM-1_920"
#define FELUCCA_OTA_DRYRUN 0
#define FELUCCA_OTA_RAMONLY 1
static uint8_t committed[112];
static int ota_wire_send(const uint8_t *p, uint32_t n);
static int ota_frame_get(const uint8_t **p, uint32_t *n) { (void)p; (void)n; return 0; }
static void ota_frame_done(void) {}
static uint32_t ota_ms;
static uint32_t ota_now_ms(void) { return ota_ms += 1000u; }
static void ota_idle(void) {}
static int ota_erase(uint32_t off) { (void)off; return 0; }
static int ota_prog(uint32_t off, const void *p, uint32_t n) { (void)off; (void)p; (void)n; return 0; }
static int ota_fread(uint32_t off, void *p, uint32_t n) { (void)off; memset(p, 0xFF, n); return 0; }
static void ota_show(uint32_t step, int32_t code) { (void)step; (void)code; }
static void ota_commit(const uint8_t *parm) { memcpy(committed, parm, 112); }
#include "../firmware/src/ota.c"
static int ota_wire_send(const uint8_t *p, uint32_t n) { (void)p; (void)n; return 0; }

int main(void)
{
    uint32_t i, n;
    stub_result_t r;

    /* CRC-32: both forms against the reference, and zlib's check value */
    stub_crc32_init(&K);
    for (i = 0; i < STUB_BODY_MAX / 4u; i++)
        body[i] = rnd() ^ rnd() << 12;
    CHECK(stub_crc32_bytes(&K, (const uint8_t *)"123456789", 9) == 0xCBF43926u, "crc32(\"123456789\")");
    for (n = 0; n <= 64u; n += 4u)
        CHECK(stub_crc32_words(&K, body + 3, n) == crc32_ref((const uint8_t *)(body + 3), n), "words, %u bytes", n);
    for (n = 0; n <= 40u; n++)
        CHECK(stub_crc32_bytes(&K, (const uint8_t *)body + 1, n) == crc32_ref((const uint8_t *)body + 1, n), "bytes, %u", n);
    CHECK(stub_crc32_words(&K, body, STUB_BODY_MAX) == crc32_ref((const uint8_t *)body, STUB_BODY_MAX), "the largest body");
    CHECK(stub_crc16((const uint8_t *)"123456789", 9) == 0x31C3u, "crc16 (XMODEM) check value");

    /* the marker */
    make_marker(BODY_LEN);
    CHECK(stub_marker_check(&K, mark, body) == STUB_MK_OK, "a good marker");
    make_marker(STUB_BODY_MAX);
    CHECK(stub_marker_check(&K, mark, body) == STUB_MK_OK, "a good marker over the largest body");
    make_marker(BODY_LEN);
    body[BODY_LEN / 4u - 1u] ^= 0x00010000u;
    CHECK(stub_marker_check(&K, mark, body) == STUB_MK_BODY, "one body bit off: the last word");
    body[BODY_LEN / 4u - 1u] ^= 0x00010000u;
    body[0] ^= 1u;
    CHECK(stub_marker_check(&K, mark, body) == STUB_MK_BODY, "one body bit off: the first word");
    body[0] ^= 1u;
    body[BODY_LEN / 4u] ^= 1u;
    CHECK(stub_marker_check(&K, mark, body) == STUB_MK_OK, "a change behind the stated length is not the marker's");
    body[BODY_LEN / 4u] ^= 1u;
    mark[0] ^= 1u;
    CHECK(stub_marker_check(&K, mark, body) == STUB_MK_MAGIC, "bad magic");
    make_marker(BODY_LEN);
    mark[12] ^= 0x80u;
    CHECK(stub_marker_check(&K, mark, body) == STUB_MK_SELF, "the body CRC field changed: the marker's own CRC");
    make_marker(BODY_LEN);
    mark[17] ^= 4u;
    CHECK(stub_marker_check(&K, mark, body) == STUB_MK_SELF, "the marker's CRC changed");
    make_marker(BODY_LEN);
    wr32(mark + 4, 2u);
    wr32(mark + 16, crc32_ref(mark, 16));
    CHECK(stub_marker_check(&K, mark, body) == STUB_MK_FORMAT, "another format, well formed");
    for (i = 0; i < 4u; i++) {                       /* lengths: 0, not a word multiple, past the room, huge */
        static const uint32_t BAD[4] = {0u, BODY_LEN + 2u, STUB_BODY_MAX + 4u, 0xFFFFFFFCu};
        make_marker(BODY_LEN);
        wr32(mark + 8, BAD[i]);
        wr32(mark + 16, crc32_ref(mark, 16));
        CHECK(stub_marker_check(&K, mark, body) == STUB_MK_LENGTH, "length %#x refused", BAD[i]);
    }
    make_marker(BODY_LEN);
    wr32(mark + 8, BODY_LEN - 4u);                   /* truncated: a valid shorter length, the CRC of the full one */
    wr32(mark + 16, crc32_ref(mark, 16));
    CHECK(stub_marker_check(&K, mark, body) == STUB_MK_BODY, "a truncated length");
    memset(mark, 0xFF, sizeof mark);
    CHECK(stub_marker_check(&K, mark, body) == STUB_MK_MAGIC, "an erased sector (0xFF)");
    for (i = 0; i < sizeof mark; i++)                /* .. as XIP shows it: the SFC cipher over 0xFF */
        mark[i] = (uint8_t)(0xFFu ^ rnd());
    CHECK(stub_marker_check(&K, mark, body) != STUB_MK_OK, "an erased sector, decrypted");
    memset(mark, 0, sizeof mark);
    CHECK(stub_marker_check(&K, mark, body) != STUB_MK_OK, "all zero");
    {   /* a torn marker page: any strict subset of the marker's 0 bits programmed (NOR: bits only go 1 -> 0) */
        uint8_t good[20];
        uint32_t t, bad = 0;
        make_marker(BODY_LEN);
        memcpy(good, mark, 20);
        for (t = 0; t < 20000u; t++) {
            uint32_t same = 1;
            for (i = 0; i < 20u; i++) {
                mark[i] = (uint8_t)(good[i] | (rnd() & rnd()));
                same &= mark[i] == good[i];
            }
            if (!same && stub_marker_check(&K, mark, body) == STUB_MK_OK)
                bad++;
        }
        CHECK(bad == 0, "torn marker pages accepted: %u of 20000", bad);
        memcpy(mark, good, 20);
    }

    /* the rescue key (version 2): 32 samples as bits (bit i = sample i, 1 = the row low) with OCT-'s column, OCT+'s
     * column and no column latched; only the last 12 count: OCT- in >= 10, OCT+ and idle in <= 1 each */
    {
        const uint32_t ALL = 0xFFFFFFFFu, TAIL = 0xFFF00000u, HEAD = 0x000FFFFFu;
        uint32_t t;
        CHECK(STUB_KEY_SAMPLES == 32u && STUB_KEY_TAIL == 12u && stub_tail(TAIL) == 12u && stub_tail(HEAD) == 0u &&
              stub_tail(ALL) == 12u && stub_tail(0x80100000u) == 2u, "the tail: samples 20..31");
        CHECK(stub_key_held(ALL, 0, 0), "OCT- held throughout");
        CHECK(stub_key_held(TAIL, 0, 0), "OCT- read open for the first 30 ms (what version 1 saw), closed after: held");
        CHECK(stub_key_held(TAIL & ~0x00500000u, 0, 0), "two bounces in the tail: 10 of 12 still counts");
        CHECK(!stub_key_held(TAIL & ~0x01500000u, 0, 0), "three: 9 of 12 does not");
        CHECK(!stub_key_held(HEAD, 0, 0), "OCT- let go before the tail: not held");
        CHECK(!stub_key_held(0, 0, 0) && !stub_key_held(0, ALL, 0) && !stub_key_held(0, 0, ALL), "no OCT-");
        CHECK(!stub_key_held(ALL, ALL, 0) && !stub_key_held(TAIL, TAIL, 0), "OCT- + OCT+: the app's calibration");
        CHECK(stub_key_held(ALL, 0x00100000u, 0), "one OCT+ read in the tail is a glitch");
        CHECK(!stub_key_held(ALL, 0x00300000u, 0), "two are a key: not the rescue");
        CHECK(!stub_key_held(ALL, 0, ALL) && !stub_key_held(ALL, ALL, ALL), "the row low with no column latched (stuck): not held");
        CHECK(stub_key_held(ALL, 0, 0x80000000u) && !stub_key_held(ALL, 0, 0xC0000000u), "idle: one glitch passes, two do not");
        for (t = 0; t < 4000u; t++) {                /* the first 20 samples, any garbage of either polarity: no effect */
            uint32_t dn = rnd() << 8 ^ rnd(), up = rnd() << 8 ^ rnd(), idle = rnd() << 8 ^ rnd();
            uint32_t g1 = (rnd() << 8 ^ rnd()) & HEAD, g2 = (rnd() << 8 ^ rnd()) & HEAD, g3 = (rnd() << 8 ^ rnd()) & HEAD;
            uint32_t want;
            if (t & 1u) { dn |= TAIL; up &= HEAD; idle &= HEAD; }          /* half of them held in the tail */
            if (t % 5u == 0u) { up &= HEAD | 0x00100000u; idle &= HEAD; }
            want = stub_key_held(dn, up, idle);
            CHECK(stub_key_held((dn & TAIL) | g1, (up & TAIL) | g2, (idle & TAIL) | g3) == want &&
                  stub_key_held(dn & TAIL, up & TAIL, idle & TAIL) == want &&
                  stub_key_held(dn | HEAD, up | HEAD, idle | HEAD) == want, "early samples changed the answer");
            CHECK(want == (stub_tail(dn) >= 10u && stub_tail(up) <= 1u && stub_tail(idle) <= 1u), "the rule");
        }
    }

    /* the staged loader */
    n = make_loader(18000u);
    CHECK(stub_loader_check(ldr) == STUB_LD_OK, "a good staged loader (%u B, 5 blocks)", n);
    CHECK(make_loader(4096u) && stub_loader_check(ldr) == STUB_LD_OK, "one full block");
    CHECK(make_loader(100u) && stub_loader_check(ldr) == STUB_LD_OK, "one short block");
    n = make_loader(18000u);
    memset(ldr, 0xFF, sizeof ldr);
    CHECK(stub_loader_check(ldr) == STUB_LD_HEAD, "an erased staging area");
    memset(ldr, 0, sizeof ldr);
    CHECK(stub_loader_check(ldr) == STUB_LD_HEAD, "all zero");
    make_loader(18000u);
    memset(ldr, 0xFF, 32);
    CHECK(stub_loader_check(ldr) == STUB_LD_HEAD, "the head not written yet (ota_stage writes it last)");
    make_loader(18000u);
    ldr[5] ^= 1u;
    CHECK(stub_loader_check(ldr) == STUB_LD_HEAD, "head: a bit off");
    make_loader(18000u);
    wr32(ldr + 8, STUB_LDR_LEN);
    stub_wr16(ldr, stub_crc16(ldr + 2, 30));
    CHECK(stub_loader_check(ldr) == STUB_LD_HEAD, "head: a length past the area");
    make_loader(18000u);
    wr32(ldr + 8, 16u);
    stub_wr16(ldr, stub_crc16(ldr + 2, 30));
    CHECK(stub_loader_check(ldr) == STUB_LD_HEAD, "head: a length below the head itself");
    make_loader(18000u);
    ldr[0x20 + 9] ^= 0x10u;
    CHECK(stub_loader_check(ldr) == STUB_LD_INNER, "inner head: a bit off");
    make_loader(18000u);
    wr32(ldr + 0x20 + 8, 0x01C0A900u);
    stub_wr16(ldr + 0x20, stub_crc16(ldr + 0x22, 30));
    CHECK(stub_loader_check(ldr) == STUB_LD_INNER, "inner head: another load address");
    make_loader(18000u);
    ldr[0x4C] = 'X';
    CHECK(stub_loader_check(ldr) == STUB_LD_NOT_OURS, "not Felucca's loader (the official one: refused)");
    n = make_loader(18000u);
    memset(ldr + n / 2u, 0xFF, sizeof ldr - n / 2u);
    CHECK(stub_loader_check(ldr) == STUB_LD_BLOCKS, "cut off half way (the rest erased)");
    make_loader(18000u);
    memset(ldr + n - 300u, 0xFF, 300);
    CHECK(stub_loader_check(ldr) == STUB_LD_OK, "(block contents are not checked: only the chain)");
    make_loader(18000u);
    wr32(ldr + 0x40 + 4, 4095u);
    CHECK(stub_loader_check(ldr) == STUB_LD_BLOCKS, "a short block that is not the last");
    make_loader(18000u);
    wr32(ldr + 0x20 + 4, 18001u);
    stub_wr16(ldr + 0x20, stub_crc16(ldr + 0x22, 30));
    CHECK(stub_loader_check(ldr) == STUB_LD_BLOCKS, "the blocks do not add up to the image");
    make_loader(18000u);
    wr32(ldr + 8, n - 3u);
    stub_wr16(ldr, stub_crc16(ldr + 2, 30));
    CHECK(stub_loader_check(ldr) == STUB_LD_BLOCKS, "the chain runs past the stated end");
    for (i = 0; i < 3000u; i++) {                    /* noise never reads past the area (ASan in run_cr_tests.sh) */
        uint32_t j;
        make_loader(18000u);
        for (j = 0; j < 6u; j++)
            ldr[0x40 + rnd() % (n - 0x40u)] = (uint8_t)rnd();
        (void)stub_loader_check(ldr);
    }

    /* the rescue word (a count), the app's requests (never a count), garbage */
    CHECK(STUB_REQ_TEST != STUB_REQ_RESCUE && stub_rescues(STUB_REQ_TEST) == 0 && stub_rescues(STUB_REQ_RESCUE) == 0 &&
          (STUB_REQ_TEST & 0xFFFFFF00u) != STUB_RSC_MAGIC && (STUB_REQ_RESCUE & 0xFFFFFF00u) != STUB_RSC_MAGIC,
          "the request words are not rescue counts");
    CHECK(stub_rescues(0) == 0 && stub_rescues(0xFFFFFFFFu) == 0 && stub_rescues(0xA5A5A5A5u) == 0 &&
          stub_rescues(1u) == 0, "garbage (power-on RAM) is not a count");
    CHECK(stub_rescues(STUB_RSC_MAGIC) == 0 && stub_rescues(STUB_RSC_MAGIC | 1u) == 1 &&
          stub_rescues(STUB_RSC_MAGIC | 0xFFu) == 255, "the magic and the count");

    /* the decision */
    make_marker(BODY_LEN);
    make_loader(18000u);
    r = run(0, 0xDEADBEEFu);
    CHECK(r.decision == STUB_BOOT && r.mk == STUB_MK_OK && r.ld == STUB_LD_SKIPPED && r.word == 0 && r.prev == 0 &&
          r.req == 0 && plain_calls == 0, "marker good, no key: boot (the loader is not looked at, the window untouched)");
    /* the app's requests (SysEx 77 STUB): the test on a boot, the rescue without a key; answered once */
    r = run(0, STUB_REQ_TEST);
    CHECK(r.decision == STUB_BOOT && r.mk == STUB_MK_OK && r.ld == STUB_LD_OK && r.word == 0 && r.prev == 0 &&
          r.req == 2 && plain_calls == 1, "test request: boot, the staged loader checked on the way (ok)");
    CHECK(stub_detail(&r, 0) == (STUB_MK_OK | (uint32_t)STUB_LD_OK << 8 | 1u << 26), "test request: the detail word");
    ldr[0x4C] ^= 1u;
    r = run(0, STUB_REQ_TEST);
    CHECK(r.decision == STUB_BOOT && r.ld == STUB_LD_NOT_OURS && r.word == 0,
          "test request, a loader the stub would refuse: still a boot, the code reported");
    ldr[0x4C] ^= 1u;
    r = run(0, STUB_REQ_RESCUE);
    CHECK(r.decision == STUB_LOADER && r.mk == STUB_MK_SKIPPED && r.ld == STUB_LD_OK && r.prev == 0 && r.req == 1 &&
          r.word == (STUB_RSC_MAGIC | 1u) && plain_calls == 1, "rescue request: the loader, as with the key (one attempt counted)");
    CHECK(stub_detail(&r, 0) == (STUB_MK_SKIPPED | (uint32_t)STUB_LD_OK << 8 | 1u << 25), "rescue request: the detail word (no key bit)");
    r = run(0, r.word);
    CHECK(r.decision == STUB_BOOT && r.prev == 1 && r.req == 0 && r.word == 0,
          "the boot after it (the marker is good): a normal boot, the request not repeated");
    r = run(1, STUB_REQ_TEST);
    CHECK(r.decision == STUB_LOADER && r.req == 2 && r.word == (STUB_RSC_MAGIC | 1u), "the key held over a test request: the rescue");
    CHECK(stub_detail(&r, 1) == (STUB_MK_SKIPPED | (uint32_t)STUB_LD_OK << 8 | 1u << 24 | 1u << 26), ".. both bits in the detail");
    r = run(0, STUB_REQ_TEST ^ 1u);
    CHECK(r.decision == STUB_BOOT && r.req == 0 && r.ld == STUB_LD_SKIPPED && plain_calls == 0, "a word one bit off a request: nothing");
    r = run(0, STUB_REQ_RESCUE ^ 0x100u);
    CHECK(r.decision == STUB_BOOT && r.req == 0, "a word one bit off the rescue request: a normal boot");
    r = run(0, STUB_RSC_MAGIC | 1u);
    CHECK(r.decision == STUB_BOOT && r.word == 0 && r.prev == 1, "a boot after a rescue: the count cleared, shown as prev");
    r = run(1, 0);
    CHECK(r.decision == STUB_LOADER && r.mk == STUB_MK_SKIPPED && r.ld == STUB_LD_OK && r.word == (STUB_RSC_MAGIC | 1u) &&
          plain_calls == 1, "key held, marker good, loader good: the loader, one attempt counted");
    r = run(1, STUB_RSC_MAGIC | 1u);
    CHECK(r.decision == STUB_UBOOT && r.word == 0 && r.prev == 1 && r.ld == STUB_LD_SKIPPED && plain_calls == 0,
          "key held, second arrival: UBOOT, the count cleared for the power-on after it");
    r = run(1, 0x12345678u);
    CHECK(r.decision == STUB_LOADER, "key held, a garbage word: a first attempt");
    mark[9] ^= 1u;
    r = run(0, 0);
    CHECK(r.decision == STUB_LOADER && r.mk == STUB_MK_SELF && r.ld == STUB_LD_OK && r.word == (STUB_RSC_MAGIC | 1u),
          "marker bad, loader good, count 0: the loader");
    r = run(0, r.word);
    CHECK(r.decision == STUB_UBOOT && r.prev == 1 && r.word == 0, "marker bad, count 1: UBOOT");
    r = run(0, STUB_RSC_MAGIC | 7u);
    CHECK(r.decision == STUB_UBOOT && r.prev == 7, "marker bad, a larger count: UBOOT");
    r = run(0, 0xFFFFFFFFu);
    CHECK(r.decision == STUB_LOADER, "marker bad, a garbage word: the loader");
    memset(ldr, 0xFF, sizeof ldr);
    r = run(0, 0);
    CHECK(r.decision == STUB_UBOOT && r.ld == STUB_LD_HEAD && r.word == 0 && plain_calls == 1,
          "marker bad, no loader: UBOOT");
    r = run(1, 0);
    CHECK(r.decision == STUB_UBOOT && r.ld == STUB_LD_HEAD, "key held, no loader: UBOOT");
    make_marker(BODY_LEN);
    r = run(0, 0);
    CHECK(r.decision == STUB_BOOT, "marker good, no loader, no key: boot (nothing staged)");
    r = run(0, STUB_REQ_RESCUE);
    CHECK(r.decision == STUB_UBOOT && r.ld == STUB_LD_HEAD && r.word == 0, "rescue request, no loader: UBOOT");
    r = run(0, STUB_REQ_TEST);
    CHECK(r.decision == STUB_BOOT && r.ld == STUB_LD_HEAD, "test request, no loader: boot, 'bad head' reported");
    memset(mark, 0xFF, sizeof mark);
    make_loader(18000u);
    r = run(0, 0);
    CHECK(r.decision == STUB_LOADER && r.mk == STUB_MK_MAGIC, "an erased marker sector: the loader");
    for (i = 0; i < 2000u; i++) {                    /* strict: nothing but STUB_MK_OK without the key boots */
        uint32_t j, key = rnd() & 1u, pick = rnd() % 6u;
        uint32_t word = pick < 2u ? STUB_RSC_MAGIC | (rnd() & 3u) : pick == 2u ? STUB_REQ_TEST : pick == 3u ? STUB_REQ_RESCUE :
                        rnd() << 8 ^ rnd();
        make_marker(BODY_LEN);
        if (rnd() & 1u)
            for (j = 0; j < 3u; j++)
                mark[rnd() % 20u] ^= (uint8_t)(1u << (rnd() & 7u));
        r = run(key, word);
        if (r.decision == STUB_BOOT)
            CHECK(!key && word != STUB_REQ_RESCUE && r.mk == STUB_MK_OK && stub_marker_check(&K, mark, body) == STUB_MK_OK,
                  "a boot without a valid marker");
        else
            CHECK(key || word == STUB_REQ_RESCUE || stub_marker_check(&K, mark, body) != STUB_MK_OK,
                  "a rescue with a valid marker, no key and no request");
        CHECK(r.req == (word == STUB_REQ_RESCUE ? 1u : word == STUB_REQ_TEST ? 2u : 0u) && (!r.req || !r.prev), "the request read");
        CHECK(r.decision != STUB_LOADER || (r.prev == 0 && r.ld == STUB_LD_OK && stub_rescues(r.word) == 1), "the loader only on a first attempt");
        CHECK(r.decision == STUB_LOADER || r.word == 0, "the word cleared on a boot and on UBOOT");
    }

    /* the record: ota.c ota_session's, byte for byte (its staging faked: the host says success at once) */
    {
        uint8_t parm[STUB_PARM_LEN], mine[112];
        uint32_t k;
        memset(parm, 0xAA, sizeof parm);
        stub_record(parm);
        for (k = 0; k < 112u; k++)                   /* ota_session up to its commit, without a host */
            mine[k] = 0;
        ota_wr16(mine + 2, 0x5A0Du);
        ota_wr16(mine + 4, 0x5A01u);
        ota_wr16(mine + 6, 0x5441u);
        memcpy(mine + 8, "ota-FM-1_015", 12);
        ota_wr32(mine + 72, OTA_AREA);
        ota_wr16(mine, ota_crc16(mine + 2, 78, 0));
        CHECK(!memcmp(parm, mine, 112), "the record == ota.c's fields");
        CHECK(STUB_PARM_LEN == 112u && STUB_LDR_OFF == OTA_AREA && STUB_LDR_LEN == OTA_AREA_LEN, "ota.c's constants");
        CHECK(stub_crc16(parm + 2, 78) == ota_crc16(parm + 2, 78, 0) && stub_rd16(parm) == ota_crc16(parm + 2, 78, 0), "its CRC");
    }

    /* the mailbox words (hal/fm1_sys.h fm1_stub_word, cr_backup.c crb_debug) */
    r = run(1, 0);
    CHECK(stub_info(STUB_BOOT) == (0x53420000u | (uint32_t)STUB_VERSION << 8 | 1u), "info: SB, version, decision");
    CHECK(stub_detail(&r, 1) == (STUB_MK_SKIPPED | (uint32_t)STUB_LD_OK << 8 | 0u << 16 | 1u << 24), "detail");
    CHECK(STUB_VERSION == 3, "this is stub version 3 (firmware/stub/stub.lock, docs/BOOT-SAFETY.md)");
    CHECK(STUB_BODY_OFF - 0x4120u == STUB_LEN && STUB_MARK_SEC + 0x1000u - 32u == STUB_MARK_OFF &&
          STUB_BODY_OFF + STUB_BODY_MAX == STUB_MARK_SEC, "the layout's constants agree");

    (void)ota_session; (void)ota_service; (void)ota_boot_cleanup; (void)committed;
    printf("cr_stub_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
