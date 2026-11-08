/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The boot stub's decision, portable (docs/BOOT-SAFETY.md "As built"): the device part is stub.c, the host tests are
 * tests/cr_stub_test.c and the loader's interruption model in tests/ldr_test.c. No data, no libcalls, no division.
 *
 * The stub is app.bin's first 0xEE0 bytes (flash 0x4120..0x4FFF, XIP 0x02000120..0x02000FFF); the SPL enters it on
 * every boot. The app proper (the body) is linked from XIP 0x02001000 (flash 0x5000). The last 32 bytes of app.bin's
 * last whole sector (flash 0x91FE0) hold the commit marker, which the loader erases before it writes anything else
 * and writes last (firmware/loader/ldr_core.c):
 *
 *   +0  magic   "CRBM"
 *   +4  format  1
 *   +8  len     the body's length from flash 0x5000, a multiple of 4, at most 0x8C000 (up to the marker's sector)
 *   +12 crc     CRC-32 (zlib's) of those len bytes, as they read through XIP (decrypted)
 *   +16 mcrc    CRC-32 of the 16 bytes above
 *   +20 comp    4 bytes that tools/fm1pkg_make.py solves so app.bin's CRC-16 in the directory is a constant (the
 *               stub does not read them)
 *
 * The decision:
 *   no rescue key and the marker valid (with its body CRC)  -> STUB_BOOT: the app at 0x02001000
 *   otherwise rescue: a first arrival with a valid staged loader at flash 0xE0000 -> STUB_LOADER (the record the
 *   app's step 1 writes, then a core reset: the SPL runs the loader); a second arrival without a boot in between
 *   (the rescue word, RAM that survives a warm reset), or no valid loader -> STUB_UBOOT (the ROM's USB boot).
 * Strict: no valid marker, no boot. The rescue key is OCT- alone held at power-on (with OCT+ it is the app's
 * calibration gesture).
 *
 * Version 2 (version 1 did not see the key on the unit: it sampled for 3 ms right after the SPL's jump and gave up
 * when OCT- read open in the first four samples): the key is sampled through a fixed window on every boot and
 * judged by its tail (stub_key_held), the samples go to the mailbox, and the running app can ask through the rescue
 * word for the loader check on a normal boot (STUB_REQ_TEST) or for the rescue itself (STUB_REQ_RESCUE).
 *
 * Version 3: the cause was found on the unit. PA2 (the LCD's "backlight" line, active low) is the enable of a rail
 * that also feeds the 595 chain and the LEDs: with PA2 high, as the SPL leaves it, no key reads closed and no LED
 * lights. The device side (stub.c) now pulls PA2 low before it samples, as the app's fm1_lcd_hw_init does, and
 * drives it high again before a hand-over. Nothing in this file changed but the version. */
#ifndef STUB_CORE_H
#define STUB_CORE_H
#define STUB_VERSION 3                   /* in the stub's header (crt0_stub.S: no C suffix) and the RAM mailbox */
#ifndef __ASSEMBLER__
#include <stdint.h>

#define STUB_LEN 0xEE0u                  /* app.bin offset of the body: flash 0x4120 + 0xEE0 = 0x5000 */
#define STUB_BODY_OFF 0x5000u            /* flash */
#define STUB_BODY_MAX 0x8C000u           /* the body ends below the marker's sector (flash 0x91000) */
#define STUB_MARK_SEC 0x91000u
#define STUB_MARK_OFF 0x91FE0u           /* the sector's last 32 bytes: in its last page, programmed last */
#define STUB_MARK_LEN 20u
#define STUB_MARK_MAGIC 0x4D425243u      /* "CRBM" */
#define STUB_MARK_FORMAT 1u
#if BOARD_SMK37
/* SMK-37 (BOARD_SMK37 from build.py; SMK_MODEL as hal/fm1_cc.h): the same app area layout as the FM-1's, the update
 * staging in the stock VM's last sectors (ota.c), and the model's name in the record */
#ifndef SMK_MODEL
#define SMK_MODEL "SMK-37 Pro"
#endif
#define STUB_LDR_OFF 0xBB000u            /* the staged loader (ota.c OTA_AREA, OTA_AREA_LEN) */
#define STUB_PARM_ID "ota-" SMK_MODEL "_016"   /* ota.c OTA_PARM_ID */
#define STUB_PLAIN_LO 0x9D000u           /* hal/fm1_flash_map.h FL_PLAIN_LO */
#else
#define STUB_LDR_OFF 0xE0000u            /* the staged loader (ota.c OTA_AREA, OTA_AREA_LEN) */
#define STUB_PLAIN_LO 0x93000u           /* hal/fm1_flash_map.h FL_PLAIN_LO */
#endif
#define STUB_LDR_LEN 0x5000u
#define STUB_PARM_LEN 112u               /* the UPDATA_PARM record (hal/fm1_sys.h FM1_UPDATA_PARM_LEN) */

#define STUB_RSC_MAGIC 0x52534300u       /* the rescue word: "RSC" in bits 8..31, the attempts in bits 0..7 */
#define STUB_REQ_TEST 0x54514552u        /* .. or a request the app left there before a restart ("REQT"): boot as
                                          * usual, and check the staged loader on the way (the detail's loader code) */
#define STUB_REQ_RESCUE 0x52514552u      /* .. ("REQR"): as if the rescue key were held */
#define STUB_INFO_MAGIC 0x53420000u      /* the mailbox's info word: "SB", the version, the decision */

enum { STUB_BOOT = 1, STUB_LOADER = 2, STUB_UBOOT = 3 };
enum { STUB_MK_OK, STUB_MK_MAGIC, STUB_MK_FORMAT, STUB_MK_SELF, STUB_MK_LENGTH, STUB_MK_BODY, STUB_MK_SKIPPED };
enum { STUB_LD_OK, STUB_LD_HEAD, STUB_LD_INNER, STUB_LD_NOT_OURS, STUB_LD_BLOCKS, STUB_LD_SKIPPED };

#ifndef STUB_PLAIN_WINDOW
#define STUB_PLAIN_WINDOW() ((void)0)    /* the device: flash above 0x93000 reads as plaintext through XIP */
#endif
#ifndef STUB_BODY_CRC
#define STUB_BODY_CRC 1                  /* 0: trust the marker alone (the loader's order makes it the commit) */
#endif

static uint32_t stub_rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t stub_rd32(const uint8_t *p) { return stub_rd16(p) | stub_rd16(p + 2) << 16; }
static void stub_wr16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* ota.c ota_crc16: poly 0x1021, init 0 (the JLFS heads, the record) */
static uint32_t stub_crc16(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0, i;
    while (n--) {
        c ^= (uint32_t)*p++ << 8;
        for (i = 0; i < 8u; i++)
            c = c & 0x8000u ? (c << 1) ^ 0x1021u : c << 1;
        c &= 0xFFFFu;
    }
    return c;
}

/* CRC-32 (zlib's: reflected 0xEDB88320, init and final xor 0xFFFFFFFF), four tables of 256 words built in RAM
 * (the caller's stack: 4 KiB; the stub has no room for them in flash and no .data) and four bytes per step. */
typedef struct { uint32_t t[4][256]; } stub_crc_t;

static void stub_crc32_init(stub_crc_t *k)
{
    uint32_t i, j, c;
    for (i = 0; i < 256u; i++) {
        c = i;
        for (j = 0; j < 8u; j++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        k->t[0][i] = c;
    }
    for (i = 0; i < 256u; i++)
        for (j = 1; j < 4u; j++)
            k->t[j][i] = (k->t[j - 1u][i] >> 8) ^ k->t[0][k->t[j - 1u][i] & 0xFFu];
}

/* words: little-endian, as the chip and every host we build on read them; n in bytes, a multiple of 4 */
static uint32_t stub_crc32_words(const stub_crc_t *k, const uint32_t *w, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (n >>= 2; n; n--) {
        c ^= *w++;
        c = k->t[3][c & 0xFFu] ^ k->t[2][(c >> 8) & 0xFFu] ^ k->t[1][(c >> 16) & 0xFFu] ^ k->t[0][c >> 24];
    }
    return ~c;
}

static uint32_t stub_crc32_bytes(const stub_crc_t *k, const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--)
        c = (c >> 8) ^ k->t[0][(c ^ *p++) & 0xFFu];
    return ~c;
}

/* the marker (mark: its 20 bytes, plain) and the body it commits (body: flash 0x5000 on, plain, word aligned) */
static uint32_t stub_marker_check(const stub_crc_t *k, const uint8_t *mark, const uint32_t *body)
{
    uint32_t len = stub_rd32(mark + 8);
    if (stub_rd32(mark) != STUB_MARK_MAGIC)
        return STUB_MK_MAGIC;                        /* (an erased sector decrypts to noise: here) */
    if (stub_rd32(mark + 16) != stub_crc32_bytes(k, mark, 16))
        return STUB_MK_SELF;                         /* a torn marker page */
    if (stub_rd32(mark + 4) != STUB_MARK_FORMAT)
        return STUB_MK_FORMAT;
    if (!len || (len & 3u) || len > STUB_BODY_MAX)
        return STUB_MK_LENGTH;
#if STUB_BODY_CRC
    if (stub_crc32_words(k, body, len) != stub_rd32(mark + 12))
        return STUB_MK_BODY;
#else
    (void)body;
#endif
    return STUB_MK_OK;
}

static uint32_t stub_memeq(const uint8_t *a, const char *b, uint32_t n)
{
    while (n--)
        if (*a++ != (uint8_t)*b++)
            return 0;
    return 1;
}

/* the staged loader (ld: flash 0xE0000..0xE4FFF, plain) as ota.c left it: the head ota_stage_head wrote last, then
 * the package's ota.bin from its byte 0x20 (the inner head with the load address, the LZ4 blocks build.py ldr_wrap
 * made). The checks of ota_stage that the flash copy allows (the package's outer body CRC is not kept in flash),
 * and the block chain from end to end, so an erased or cut-off staging area is not handed to the SPL. Only our
 * loader passes (crt0_ldr.S's "FELUCCA-LOADER-1"): the official one rewrites the head and the stub with it. */
static uint32_t stub_loader_check(const uint8_t *ld)
{
    uint32_t len, isize, p, end, total = 0, i, own = 0;
    if (stub_crc16(ld + 2, 30) != stub_rd16(ld) || stub_rd32(ld + 4) != STUB_LDR_OFF || ld[12] != 0x41u ||
        !stub_memeq(ld + 16, "LOADER.BIN", 11))
        return STUB_LD_HEAD;
    len = stub_rd32(ld + 8) - 32u;                   /* ota.bin's body: the inner head and the blocks */
    if (len < 32u + 8u || len > STUB_LDR_LEN - 0x200u)
        return STUB_LD_HEAD;
    isize = stub_rd32(ld + 0x20 + 4);
    if (stub_crc16(ld + 0x22, 30) != stub_rd16(ld + 0x20) || stub_rd32(ld + 0x20 + 8) != 0x01C0A800u ||
        !isize || isize > 0x14000u)
        return STUB_LD_INNER;
    for (i = 0x48; i + 16u <= 0x48u + 64u && !own; i++)
        own = stub_memeq(ld + i, "FELUCCA-LOADER-1", 16);
    if (!own)
        return STUB_LD_NOT_OURS;
    for (p = 0x40, end = 0x20u + len; p + 8u <= end;) {
        uint32_t clen = stub_rd32(ld + p), dlen = stub_rd32(ld + p + 4);
        if (!clen || clen > 4096u || !dlen || dlen > 4096u || clen > end - p - 8u)
            return STUB_LD_BLOCKS;
        p += 8u + clen;
        total += dlen;
        if (dlen != 4096u && p != end)
            return STUB_LD_BLOCKS;                   /* only the last block is short */
    }
    return p == end && total == isize ? STUB_LD_OK : STUB_LD_BLOCKS;
}

/* the UPDATA_PARM record, byte for byte ota.c ota_session's */
static void stub_record(uint8_t *parm)
{
#if BOARD_SMK37
    static const char ID[] = STUB_PARM_ID;
#else
    static const char ID[12] = {'o', 't', 'a', '-', 'F', 'M', '-', '1', '_', '0', '1', '5'};
#endif
    uint32_t i;
    for (i = 0; i < STUB_PARM_LEN; i++)
        parm[i] = 0;
    stub_wr16(parm + 2, 0x5A0Du);
    stub_wr16(parm + 4, 0x5A01u);
    stub_wr16(parm + 6, 0x5441u);
#if BOARD_SMK37
    for (i = 0; i < sizeof ID - 1u; i++)
#else
    for (i = 0; i < 12u; i++)
#endif
        parm[8 + i] = (uint8_t)ID[i];
    stub_wr16(parm + 72, STUB_LDR_OFF & 0xFFFFu);
    stub_wr16(parm + 74, STUB_LDR_OFF >> 16);
    stub_wr16(parm, stub_crc16(parm + 2, 78));
}

/* rescue attempts since the last boot: 0 unless the word carries the magic (power-on RAM is not a count) */
static uint32_t stub_rescues(uint32_t word)
{
    return (word & 0xFFFFFF00u) == STUB_RSC_MAGIC ? word & 0xFFu : 0u;
}

/* the rescue key from 32 samples about 1.5 ms apart (48 ms, every boot), bit i = sample i, 1 = the row low: dn
 * with OCT-'s column latched, up with OCT+'s, idle with no column latched (a row stuck low, or the matrix not yet
 * alive, reads closed there too). Only the tail counts, the last 12 samples (30 ms into the window, 18 ms long):
 * OCT- closed in at least 10 of them, OCT+ and the idle read in at most 1 each. Whatever the first 20 samples
 * read, of either polarity, changes nothing: they are the time the rail behind PA2 has to come up (stub.c
 * shw_rail; until it is up a held key reads open, as versions 1 and 2 read it throughout). Both keys: the app's
 * calibration, not ours. */
#define STUB_KEY_SAMPLES 32u
#define STUB_KEY_TAIL 12u
static uint32_t stub_tail(uint32_t bits)
{
    uint32_t i, n = 0;
    for (i = STUB_KEY_SAMPLES - STUB_KEY_TAIL; i < STUB_KEY_SAMPLES; i++)
        n += (bits >> i) & 1u;
    return n;
}
static uint32_t stub_key_held(uint32_t dn, uint32_t up, uint32_t idle)
{
    return stub_tail(dn) >= STUB_KEY_TAIL - 2u && stub_tail(up) <= 1u && stub_tail(idle) <= 1u;
}

typedef struct {
    const uint8_t *mark;                 /* flash STUB_MARK_OFF, plain */
    const uint32_t *body;                /* flash STUB_BODY_OFF, plain */
    const uint8_t *ldr;                  /* flash STUB_LDR_OFF, plain once STUB_PLAIN_WINDOW() ran */
} stub_view_t;

typedef struct {
    uint32_t decision;                   /* STUB_BOOT / STUB_LOADER / STUB_UBOOT */
    uint32_t mk, ld;                     /* STUB_MK_*, STUB_LD_* */
    uint32_t prev;                       /* the rescue attempts found */
    uint32_t req;                        /* the app's request found: 0, 1 STUB_REQ_RESCUE, 2 STUB_REQ_TEST */
    uint32_t word;                       /* the rescue word to leave in RAM */
} stub_result_t;

/* the whole decision; key: stub_key_held's answer; word: the rescue word as found (a count, a request, or
 * anything: power-on RAM is neither) */
static void stub_decide(const stub_view_t *v, stub_crc_t *k, uint32_t key, uint32_t word, stub_result_t *r)
{
    r->prev = stub_rescues(word);
    r->req = word == STUB_REQ_RESCUE ? 1u : word == STUB_REQ_TEST ? 2u : 0u;
    r->ld = STUB_LD_SKIPPED;
    r->mk = STUB_MK_SKIPPED;
    r->word = 0;                                     /* a boot, or the ROM: the next rescue is a first one again;
                                                      * a request is answered once */
    if (!key && r->req != 1u) {
        stub_crc32_init(k);
        r->mk = stub_marker_check(k, v->mark, v->body);
        if (r->mk == STUB_MK_OK) {
            if (r->req == 2u) {                      /* the test: the rescue's read of the loader, then the boot */
                STUB_PLAIN_WINDOW();
                r->ld = stub_loader_check(v->ldr);
            }
            r->decision = STUB_BOOT;
            return;
        }
    }
    r->decision = STUB_UBOOT;
    if (r->prev)                                     /* the hand-over was tried and we are here again */
        return;
    STUB_PLAIN_WINDOW();
    r->ld = stub_loader_check(v->ldr);
    if (r->ld != STUB_LD_OK)
        return;
    r->decision = STUB_LOADER;
    r->word = STUB_RSC_MAGIC | 1u;
}

/* the mailbox's words 1 and 3 (hal/fm1_sys.h fm1_stub_word; SysEx 73 DEBUG shows them). Detail: the marker's
 * code, the loader's << 8, the rescue attempts found << 16, bit 24 the rescue key, bit 25 the app asked for the
 * rescue, bit 26 the app asked for the test */
static uint32_t stub_info(uint32_t decision) { return STUB_INFO_MAGIC | (uint32_t)STUB_VERSION << 8 | decision; }
static uint32_t stub_detail(const stub_result_t *r, uint32_t key)
{
    return r->mk | r->ld << 8 | r->prev << 16 | key << 24 | r->req << 25;
}
#endif /* __ASSEMBLER__ */
#endif
