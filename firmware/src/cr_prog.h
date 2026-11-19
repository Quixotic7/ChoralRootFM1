/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Progressions (GitHub issue 64, Ripchord style): KEY layer page 2 Keyboard: Prog makes every key of the keybed play
 * the chord stored for it in the selected progression. This header is the contract between the store (cr_prog.c),
 * the engine / UI (cr_engine.c, cr_ui.c) and the web editor (web/fm1prog.js); docs/PROGRESSIONS.md has the prose.
 *
 * The object (storage.c OBJ_PROG, one A/B pair: FM-1 0xB2000 / 0xB3000, SMK-37 0xB3000 / 0xFE000; backup id
 * CRPG_BACKUP_ID), all little-endian, at most CRPG_SIZE bytes:
 *   header     16 bytes   magic u32 CRPG_MAGIC, version u8 CRPG_VERSION, n u8 (= CRPG_N), used u16 (the object's length
 *                         in bytes, header included), rsv[8] 0
 *   directory  CRPG_N x 16 name[CRPG_NAME] (ASCII 0x20..0x7E, 0-padded, need not end in 0), base u8 (the Ripchord
 *                         trigger note of key 0, kept for the .rpc round trip; 0..127), nkeys u8 (0..CRPG_KEYS; 0 = an
 *                         empty slot, then off is 0 and the name is all 0), off u16 (its data's offset from the start)
 *   data       per used slot, at off: nkeys records [n u8 0..CRPG_NOTES][n notes, ascending, distinct, 0..127]; n = 0
 *                         is a key with no chord (silent). The slots' data do not overlap and lie inside [16 + 16 CRPG_N,
 *                         used).
 * Anything else (another magic or version, n != CRPG_N, used > CRPG_SIZE or < the header and directory, a record out of
 * range, notes not ascending) makes the whole object invalid: the unit then uses the factory set (crp_factory).
 *
 * Key -> chord: trigger k (0 = the first root key with no chord key in use: C4 on the FM-1, its chord keys F3..B3 keeping their
 * jobs; C3 on the SMK-37) plays progression key
 * i = k + 12 * window (window 0..3: OCT- / OCT+ in Prog). i >= nkeys or n = 0: the key is silent. */
#ifndef CR_PROG_H
#define CR_PROG_H
#include <stdint.h>

#define CRPG_MAGIC 0x31505243u      /* "CRP1" */
#define CRPG_VERSION 1u
#define CRPG_N 16u                  /* progressions */
#define CRPG_KEYS 48u               /* keys one progression spans at most */
#define CRPG_NOTES 8u               /* notes in one chord at most (cr_engine.h CR_CHORD_MAX) */
#define CRPG_NAME 12u
#define CRPG_HDR 16u
#define CRPG_DIRENT 16u
#define CRPG_DATA (CRPG_HDR + CRPG_N * CRPG_DIRENT)   /* 272: the first data byte */
#define CRPG_SIZE 3840u             /* the object's payload at most (storage.c ST_PAYLOAD_MAX) */
#define CRPG_BACKUP_ID 24u          /* the backup protocol's object id (web/EDITOR_PROTOCOL.md) */
#define CRPG_WINDOWS 4u             /* OCT windows: 0, 12, 24, 36 keys along */

/* a stored object of n bytes: 1 valid (as above), 0 not */
int  crp_valid(const uint8_t *blk, uint32_t n);
/* the factory set (six progressions of ChoralRoot's own, slots 1..6; 7..16 empty) into blk (CRPG_SIZE bytes); its
 * length */
uint32_t crp_factory(uint8_t *blk);

/* the RAM copy the engine and the UI read (no flash access after these: the audio side may resolve a loop's chord) */
void crp_init(void);                /* power-on: the stored object if valid, else the factory set */
void crp_reload(void);              /* after a backup commit of CRPG_BACKUP_ID: as crp_init */

unsigned crp_nkeys(unsigned p);     /* p 0..CRPG_N-1: the keys it spans (0: empty) */
unsigned crp_count(unsigned p);     /* .. the keys that hold a chord */
void crp_name(unsigned p, char out[CRPG_NAME + 1]);   /* its name, 0-terminated ("" when empty) */
/* the chord on key i of progression p: its notes (ascending) into notes[], their count; 0 = none (silent) */
unsigned crp_chord(unsigned p, unsigned i, uint8_t notes[CRPG_NOTES]);

#endif
