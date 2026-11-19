/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of firmware/src/cr_prog.c, the progressions' store (cr_prog.h, docs/PROGRESSIONS.md), through storage.c
 * on a simulated NOR:
 *   cc -std=c11 -Wall -Wextra -Werror -pedantic -o build/host/cr_prog_test tests/cr_prog_test.c && build/host/cr_prog_test
 * (tests/run_cr_tests.sh runs it; the backup's id 24: tests/cr_backup_test.c.)
 * Checks: the factory set (valid, its names, counts, voicings: bass lowest, ascending, at most 8 notes); every rule of
 * crp_valid; the lookups on a hand-built object (silent keys, i >= nkeys, empty slots, p out of range); crp_init: the
 * stored object, the factory set when none / invalid / SAFE MODE / a 0.21 record in the sectors, and no flash write. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int passed, failed;
static void ok(int c, const char *n)
{
    if (c) passed++;
    else { failed++; printf("FAIL %s\n", n); }
}

/* ------------------------------------------------- a NOR for storage.c --- */
static uint8_t nor[0x100000];
static int writes;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); writes++; return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    for (i = 0; i < n; i++) nor[off + i] &= ((const uint8_t *)src)[i];
    writes++;
    return 0;
}
static int blocked;
#define ST_BLOCKED() blocked
#define __attribute__(x)
#define FELUCCA_FLASH 1
#define FELUCCA_CZ 1                               /* ChoralRoot's object list (OBJ_PROG is in it) */
#define FELUCCA_QUAD 1
#include "../firmware/src/storage.c"
#include "../firmware/src/cr_prog.c"
#undef __attribute__

static uint8_t b[CRPG_SIZE], f[CRPG_SIZE];

/* a hand-built object: slot 0 "Two" base 60, 3 keys (C E G / silent / 8 notes); slot 3 "", 1 key (one note);
 * slot 15 "Last Slot 16", 2 keys (silent, silent); the rest empty. -> its length */
static uint32_t build(uint8_t *o)
{
    uint32_t at = CRPG_DATA, i;
    uint8_t *d;
    memset(o, 0, CRPG_SIZE);
    o[0] = 'C'; o[1] = 'R'; o[2] = 'P'; o[3] = '1'; o[4] = 1; o[5] = 16;
    d = o + CRPG_HDR;
    memcpy(d, "Two", 3); d[12] = 60; d[13] = 3; d[14] = (uint8_t)at; d[15] = (uint8_t)(at >> 8);
    o[at++] = 3; o[at++] = 60; o[at++] = 64; o[at++] = 67;
    o[at++] = 0;
    o[at++] = 8;
    for (i = 0; i < 8; i++) o[at++] = (uint8_t)(40 + 3 * i);
    d = o + CRPG_HDR + 3 * CRPG_DIRENT;
    d[12] = 0; d[13] = 1; d[14] = (uint8_t)at; d[15] = (uint8_t)(at >> 8);
    o[at++] = 1; o[at++] = 127;
    at += 5;                                       /* (a gap: allowed) */
    d = o + CRPG_HDR + 15 * CRPG_DIRENT;
    memcpy(d, "Last Slot 16", 12); d[12] = 127; d[13] = 2; d[14] = (uint8_t)at; d[15] = (uint8_t)(at >> 8);
    o[at++] = 0; o[at++] = 0;
    o[6] = (uint8_t)at; o[7] = (uint8_t)(at >> 8);
    return at;
}
static uint8_t *ent(uint8_t *o, uint32_t p) { return o + CRPG_HDR + p * CRPG_DIRENT; }
static uint32_t off_of(uint8_t *o, uint32_t p) { return ent(o, p)[14] | (uint32_t)ent(o, p)[15] << 8; }
static void set_off(uint8_t *o, uint32_t p, uint32_t v) { ent(o, p)[14] = (uint8_t)v; ent(o, p)[15] = (uint8_t)(v >> 8); }
static void set_used(uint8_t *o, uint32_t v) { o[6] = (uint8_t)v; o[7] = (uint8_t)(v >> 8); }
static void load_ram(const uint8_t *o, uint32_t n)  /* the RAM copy = o (as crp_load leaves it) */
{
    memset(crp_x.blk, 0, sizeof crp_x.blk);
    memcpy(crp_x.blk, o, n);
    crp_x.len = (uint16_t)n;
}
/* a mutation of the hand-built object: valid? */
static int mut_valid(void (*m)(uint8_t *o, uint32_t *n))
{
    uint32_t n = build(b);
    m(b, &n);
    return crp_valid(b, n);
}
static void m_none(uint8_t *o, uint32_t *n) { (void)o; (void)n; }
static void m_magic(uint8_t *o, uint32_t *n) { (void)n; o[3] = '2'; }
static void m_version(uint8_t *o, uint32_t *n) { (void)n; o[4] = 2; }
static void m_version0(uint8_t *o, uint32_t *n) { (void)n; o[4] = 0; }
static void m_count(uint8_t *o, uint32_t *n) { (void)n; o[5] = 15; }
static void m_rsv(uint8_t *o, uint32_t *n) { (void)n; o[12] = 1; }
static void m_used_big(uint8_t *o, uint32_t *n) { set_used(o, *n + 1); }          /* used past the object */
static void m_used_small(uint8_t *o, uint32_t *n) { set_used(o, *n - 1); }        /* used short of it */
static void m_used_hdr(uint8_t *o, uint32_t *n) { set_used(o, CRPG_DATA - 1); *n = CRPG_DATA - 1; }
static void m_used_max(uint8_t *o, uint32_t *n) { set_used(o, CRPG_SIZE + 1); *n = CRPG_SIZE + 1; }
static void m_n_max(uint8_t *o, uint32_t *n) { set_used(o, CRPG_SIZE); *n = CRPG_SIZE; }   /* (valid: the size limit) */
static void m_rec_past(uint8_t *o, uint32_t *n) { o[*n - 1] = 1; }                 /* slot 15's last key: one note past used */
static void m_off_past(uint8_t *o, uint32_t *n) { set_off(o, 15, *n); }
static void m_off_low(uint8_t *o, uint32_t *n) { (void)n; set_off(o, 0, CRPG_DATA - 1); }
static void m_off_dir(uint8_t *o, uint32_t *n) { (void)n; set_off(o, 0, 0); }
static void m_overlap(uint8_t *o, uint32_t *n) { (void)n; set_off(o, 15, off_of(o, 0) + 4); }   /* inside slot 0's data */
static void m_overlap_same(uint8_t *o, uint32_t *n) { (void)n; set_off(o, 3, off_of(o, 0)); }
static void m_not_asc(uint8_t *o, uint32_t *n) { (void)n; o[CRPG_DATA + 2] = 60; }   /* 60 60 67 */
static void m_desc(uint8_t *o, uint32_t *n) { (void)n; o[CRPG_DATA + 3] = 63; }      /* 60 64 63 */
static void m_note_big(uint8_t *o, uint32_t *n) { (void)n; o[CRPG_DATA + 3] = 128; }
static void m_n9(uint8_t *o, uint32_t *n) { (void)n; o[CRPG_DATA + 5] = 9; }         /* the third key: 9 notes */
static void m_nkeys49(uint8_t *o, uint32_t *n) { (void)n; ent(o, 0)[13] = 49; }
static void m_nkeys_more(uint8_t *o, uint32_t *n) { (void)n; ent(o, 15)[13] = 3; }   /* one record past used */
static void m_base(uint8_t *o, uint32_t *n) { (void)n; ent(o, 0)[12] = 128; }
static void m_name_ctl(uint8_t *o, uint32_t *n) { (void)n; ent(o, 0)[1] = 0x1F; }
static void m_name_del(uint8_t *o, uint32_t *n) { (void)n; ent(o, 0)[1] = 0x7F; }
static void m_name_gap(uint8_t *o, uint32_t *n) { (void)n; ent(o, 0)[5] = 'x'; }      /* "Two\0\0x": not 0-padded */
static void m_empty_off(uint8_t *o, uint32_t *n) { (void)n; set_off(o, 1, CRPG_DATA); }
static void m_empty_name(uint8_t *o, uint32_t *n) { (void)n; ent(o, 1)[0] = 'A'; }
static void m_empty_base(uint8_t *o, uint32_t *n) { (void)n; ent(o, 1)[12] = 99; }  /* (valid: base not checked further) */
static void m_nkeys0(uint8_t *o, uint32_t *n) { (void)n; ent(o, 3)[13] = 0; }       /* slot 3 now empty but its off set */
static void m_silent_all(uint8_t *o, uint32_t *n)   /* (valid) slot 0's first key silent: a 0 and the rest moved */
{
    o[CRPG_DATA] = 0;
    memmove(o + CRPG_DATA + 1, o + CRPG_DATA + 4, *n - CRPG_DATA - 4);
    set_off(o, 3, off_of(o, 3) - 3);
    set_off(o, 15, off_of(o, 15) - 3);
    *n -= 3;
    set_used(o, *n);
}

int main(void)
{
    uint32_t n, p, i, k;
    uint8_t notes[CRPG_NOTES + 4];
    char name[CRPG_NAME + 1];

    /* ---- the factory set ---- */
    {
        static const char *const NM[6] = {"Pop Four", "Jazz ii-V-I", "Neo Soul", "Gospel Walk", "Minor Epic", "Dorian Days"};
        uint32_t good = 1;
        memset(f, 0xEE, sizeof f);
        n = crp_factory(f);
        ok(n == CRPG_DATA + 6u * 8u * 6u && crp_valid(f, n), "factory: valid, 560 bytes (6 x 8 keys of 5 notes)");
        for (i = n; i < CRPG_SIZE; i++) good &= f[i] == 0;
        ok(good, "factory: zeros past its length (the whole buffer written)");
        load_ram(f, n);
        for (p = 0, good = 1; p < CRPG_N; p++) {
            crp_name(p, name);
            if (p < 6) {
                good &= !strcmp(name, NM[p]) && crp_nkeys(p) == 8 && crp_count(p) == 8 && ent(f, p)[12] == 48;
                for (k = 0; k < 8; k++) {
                    uint32_t m = crp_chord(p, k, notes);
                    good &= m >= 3 && m <= CRPG_NOTES && notes[0] >= 36 && notes[0] <= 52;
                    for (i = 1; i < m; i++) good &= notes[i] > notes[i - 1] && notes[i] >= 52 && notes[i] <= 76;
                }
            } else {
                good &= !name[0] && crp_nkeys(p) == 0 && crp_count(p) == 0 && crp_chord(p, 0, notes) == 0;
                for (i = 0; i < CRPG_DIRENT; i++) good &= ent(f, p)[i] == 0;
            }
        }
        ok(good, "factory: slots 1..6 named, 8 keys from C3, every chord 3..8 notes, the bass 36..52 lowest, the rest "
                 "ascending in 52..76; slots 7..16 empty (all zero)");
        n = crp_chord(0, 0, notes);
        ok(n == 5 && notes[0] == 36 && notes[1] == 52 && notes[4] == 64, "factory: Pop Four's first key is C over C2");
        n = crp_chord(1, 3, notes);                  /* A7b9: A, G, Bb, C#, E */
        ok(n == 5 && notes[0] == 45 && notes[1] == 55 && notes[2] == 58 && notes[3] == 61 && notes[4] == 64,
           "factory: Jazz ii-V-I's key 4 is A7b9 (A2 G3 Bb3 C#4 E4)");
        ok(crp_chord(0, 8, notes) == 0 && crp_chord(5, 47, notes) == 0, "factory: keys past 8 silent");
    }

    /* ---- crp_valid ---- */
    n = build(b);
    ok(crp_valid(b, n), "hand-built: valid");
    ok(!crp_valid(b, n - 1) && !crp_valid(b, n + 1) && !crp_valid(0, n), "n other than used: invalid");
    ok(!mut_valid(m_magic), "another magic: invalid");
    ok(!mut_valid(m_version) && !mut_valid(m_version0), "another version (2, 0): invalid");
    ok(!mut_valid(m_count), "n != 16: invalid");
    ok(!mut_valid(m_rsv), "a reserved header byte not 0: invalid");
    ok(!mut_valid(m_used_big) && !mut_valid(m_used_small), "used past / short of the object: invalid");
    ok(!mut_valid(m_used_hdr), "used shorter than the header and directory: invalid");
    ok(!mut_valid(m_used_max), "used > CRPG_SIZE: invalid");
    ok(mut_valid(m_n_max), "used = CRPG_SIZE (zeros after the data): valid");
    ok(!mut_valid(m_rec_past) && !mut_valid(m_nkeys_more), "a record past used: invalid");
    ok(!mut_valid(m_off_past) && !mut_valid(m_off_low) && !mut_valid(m_off_dir), "off past used / in the directory: invalid");
    ok(!mut_valid(m_overlap) && !mut_valid(m_overlap_same), "two slots' data overlapping: invalid");
    ok(!mut_valid(m_not_asc) && !mut_valid(m_desc), "notes not ascending / not distinct: invalid");
    ok(!mut_valid(m_note_big), "a note > 127: invalid");
    ok(!mut_valid(m_n9), "a chord of 9 notes: invalid");
    ok(!mut_valid(m_nkeys49), "nkeys 49: invalid");
    ok(!mut_valid(m_base), "base 128: invalid");
    ok(!mut_valid(m_name_ctl) && !mut_valid(m_name_del) && !mut_valid(m_name_gap), "a name byte outside 0x20..0x7E, or "
       "not 0-padded: invalid");
    ok(!mut_valid(m_empty_off) && !mut_valid(m_empty_name) && !mut_valid(m_nkeys0),
       "an empty slot with an off or a name: invalid");
    ok(mut_valid(m_empty_base) && mut_valid(m_silent_all) && mut_valid(m_none), "an empty slot's base, a silent first key: valid");
    {
        uint32_t good = 1;
        n = build(b);
        for (i = CRPG_DATA; i < n && good; i++)      /* every truncation of the object: invalid */
            good = !crp_valid(b, i);
        ok(good, "every truncation (used kept): invalid");
    }

    /* ---- the lookups on the hand-built object ---- */
    n = build(b);
    load_ram(b, n);
    crp_name(0, name);
    ok(!strcmp(name, "Two") && crp_nkeys(0) == 3 && crp_count(0) == 2, "slot 1: 'Two', 3 keys, 2 with a chord");
    ok(crp_chord(0, 0, notes) == 3 && notes[0] == 60 && notes[1] == 64 && notes[2] == 67, "slot 1 key 0: C E G");
    memset(notes, 0xAA, sizeof notes);
    ok(crp_chord(0, 1, notes) == 0 && notes[0] == 0xAA, "slot 1 key 1: silent (notes untouched)");
    ok(crp_chord(0, 2, notes) == 8 && notes[0] == 40 && notes[7] == 61 && notes[8] == 0xAA, "slot 1 key 2: 8 notes, no more written");
    ok(crp_chord(0, 3, notes) == 0 && crp_chord(0, 47, notes) == 0 && crp_chord(0, 1000, notes) == 0, "slot 1: i >= nkeys silent");
    crp_name(3, name);
    ok(!name[0] && crp_nkeys(3) == 1 && crp_count(3) == 1 && crp_chord(3, 0, notes) == 1 && notes[0] == 127,
       "slot 4: no name, one key of one note (127)");
    crp_name(15, name);
    ok(!strcmp(name, "Last Slot 16") && crp_nkeys(15) == 2 && crp_count(15) == 0 && crp_chord(15, 1, notes) == 0,
       "slot 16: a 12-character name without its 0, two silent keys");
    crp_name(1, name);
    ok(!name[0] && crp_nkeys(1) == 0 && crp_count(1) == 0 && crp_chord(1, 0, notes) == 0, "slot 2: empty");
    crp_name(16, name);
    ok(!name[0] && crp_nkeys(16) == 0 && crp_count(16) == 0 && crp_chord(16, 0, notes) == 0 && crp_chord(99, 0, notes) == 0,
       "p >= 16: nothing");
    {   /* a broken RAM copy (as a torn reload could leave it): the lookups stay inside it */
        memset(crp_x.blk, 0xFF, sizeof crp_x.blk);
        crp_x.len = CRPG_SIZE;
        for (p = 0, k = 0; p < CRPG_N; p++) {
            k += crp_nkeys(p) + crp_count(p);
            for (i = 0; i < CRPG_KEYS; i++) k += crp_chord(p, i, notes);
            crp_name(p, name);
        }
        ok(k == 0, "a RAM copy of 0xFF bytes: no keys, no chord, no read outside it");
    }

    /* ---- crp_init: flash -> the RAM copy ---- */
    memset(nor, 0xFF, sizeof nor);
    writes = 0;
    memset(&crp_x, 0x5A, sizeof crp_x);
    crp_init();
    n = crp_factory(f);
    ok(!crp_x.stored && crp_x.len == n && !memcmp(crp_x.blk, f, n) && writes == 0,
       "power-on, nothing stored: the factory set, no flash write");
    ok(st_sector(OBJ_PROG, 0) == 0xB2000u && st_sector(OBJ_PROG, 1) == 0xB3000u && OBJ_PROG == 21u && OBJ_COUNT == 22u &&
       st_live(OBJ_PROG), "OBJ_PROG: 21, the FM-1's 0xB2000 / 0xB3000");
    n = build(b);
    ok(st_save(OBJ_PROG, b, n) == 0, "a stored object");
    writes = 0;
    crp_init();
    ok(crp_x.stored && crp_x.len == n && !memcmp(crp_x.blk, b, n) && crp_nkeys(0) == 3 && writes == 0,
       "power-on: the stored object, no write");
    blocked = 1;
    crp_reload();
    ok(!crp_x.stored && crp_x.len == crp_factory(f) && crp_nkeys(0) == 8, "SAFE MODE: the factory set");
    blocked = 0;
    b[3] = 'X';
    ok(st_save(OBJ_PROG, b, n) == 0, "an invalid object stored (another magic)");
    crp_reload();
    ok(!crp_x.stored && crp_nkeys(0) == 8 && crp_count(0) == 8, "invalid in flash: the factory set");
    ok(st_save(OBJ_PROG, b, 0) == 0, "an empty object stored (a backup's 0-byte PUT)");
    crp_reload();
    ok(!crp_x.stored && crp_nkeys(0) == 8, "empty: the factory set");
    {   /* a 0.21 record in the sectors: FM TONE's store at 0xB2000 (type 17, OBJ_X_QUADSTORE), CZ-1 store 2 (type 8) at
         * 0xB3000, both valid commit records with valid payloads: another object's, so refused */
        static const uint8_t pl[64] = {1, 2, 3};
        st_hdr_t h;
        uint32_t c;
        memset(nor + 0xB2000u, 0xFF, 0x2000u);
        for (c = 0; c < 2u; c++) {
            memset(&h, 0, sizeof h);
            h.magic = ST_MAGIC; h.type = (uint16_t)(c ? OBJ_X_CZSTORE1 : OBJ_X_QUADSTORE); h.slot = (uint16_t)c;
            h.seq = 5; h.len = sizeof pl; h.crc = st_crc32(pl, sizeof pl); h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
            h.hcrc = st_crc32(&h, sizeof h - 4u);
            memcpy(nor + 0xB2000u + c * 0x1000u, &h, sizeof h);
            memcpy(nor + 0xB2000u + c * 0x1000u + ST_PAYLOAD_OFF, pl, sizeof pl);
        }
        writes = 0;
        crp_reload();
        ok(!crp_x.stored && crp_nkeys(0) == 8 && writes == 0 && st_load(OBJ_PROG, b, sizeof b) < 0,
           "0.21's records in the sectors (types 17 and 8): refused by their header, the factory set, nothing written");
        n = build(b);
        ok(st_save(OBJ_PROG, b, n) == 0, "a save over them (copy A erased first)");
        crp_reload();
        ok(crp_x.stored && crp_nkeys(15) == 2, ".. read back");
    }

    printf("cr_prog_test: %d passed, %d failed\n", passed, failed);
    return failed != 0;
}
