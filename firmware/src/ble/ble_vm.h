/* SPDX-License-Identifier: GPL-3.0-only */
/* The radio's stored calibration (route C, docs/BLE-STACK.md §8.2): a read-only reader of stock V15's SDK "VM" store
 * and the copy Optimist keeps of the four RF records. Written from docs/BLE-HW-FACTS.md §14 / §15.4 (branch
 * feat/ble-facts, d907ce3), not from vendor code. Portable C, no C library; host-tested in tests/ble_vm_test.c.
 *
 *   VM      where: a short list of candidate areas, the first that starts 55 AA AA 55 AND whose first record (length
 *           > 0, inside the area) passes its check is the live one (the magic alone is not enough: other data may
 *           sit at a candidate, e.g. UP_FM6's voices at 0x095000 on an FM-1, which must never read as a VM):
 *             0x0E8000, 4 KiB: measured on an FM-1 (2026-10-08, console 'flr' after stock V15 booted, then Optimist
 *                      installed by stock's updater): the magic and the six records 106 107 187 108 113 109; the
 *                      next sector, 0x0E9000, is BTIF (record 102), so this area cannot be longer than 4 KiB;
 *             0x0E7000, 4 KiB: NOT measured, only a candidate for the second area (read FF on that unit);
 *             0x093000 / 0x095000, 8 KiB each: the emulator's layout (§14.1; read FF on the FM-1), kept so the
 *                      emulator's runs still find their VM (both marked: 0x093000).
 *           Where stock's second area is on hardware, and its size there, are not known.
 *           Records packed from area + 4: [check = low byte of CRC-16/XMODEM of the data] [id 7:0]
 *           [id 11:8 | len 3:0 << 4] [len 11:4], then the data. The log ends at the first failing check (an erased
 *           header always fails) or a record past the area; the last valid record of an id wins (§14.2, §14.3).
 *   RF      106 (2 B, crystal trims), 107 (7 B, PA), 108 (20 B, PA digital gain), 187 (68 B: 64 + its own
 *           CRC-16/XMODEM little-endian + 00 00) (§14.5). A set is complete when all four are there with these
 *           lengths and 187's inner CRC holds.
 *   copy    100 bytes kept with the settings (storage/project.c persist_t): mark, source area, the four records'
 *           data (97 B), CRC-16/XMODEM of the first 98 bytes, little-endian (§15.4 step 2).
 *   choice  the VM's set, else the copy, else none: with none the radio is never started (§15.4 step 3).
 *
 * Nothing here writes flash. */
#ifndef BLE_VM_H
#define BLE_VM_H
#include <stdint.h>
#include "ble_cfg.h"

#define BLE_VM_HW 0x0E8000u              /* stock V15's VM on an FM-1 (measured, see above) */
#define BLE_VM_HW_SIZE 0x1000u           /* (0x0E9000 is BTIF) */
#define BLE_VM_HW2 0x0E7000u             /* a candidate for the second area: not measured */
#define BLE_VM_EMU_A 0x093000u           /* the emulator's area A (§14.1) */
#define BLE_VM_EMU_B 0x095000u           /* the emulator's area B */
#define BLE_VM_EMU_SIZE 0x2000u          /* the emulator's areas: 8 KiB each */
#define BLE_VM_NCAND 4u
#define BLE_VM_MAXAREA 0x2000u           /* the largest candidate */
#define BLE_VM_MAXREC 0x1000u            /* (a 12-bit length) */

/* reads n bytes of flash at offset off into dst -> 0 ok */
typedef int (*ble_vm_read_fn)(void *ctx, uint32_t off, uint8_t *dst, uint32_t n);

struct ble_rf_trims {                    /* the four records' data, in id order */
    uint8_t x106[2], x107[7], x108[20], x187[68];
};
enum { BLE_RF_TRIMS_SIZE = 97 };

struct ble_vm_info {
    uint32_t area;                       /* the live area's flash offset; 0: no VM (no candidate marked) */
    uint32_t size;                       /* that area's size (the log cannot run past it) */
    uint32_t end;                        /* the end of its log, from the area start */
    uint16_t nrec;                       /* valid records in the log */
    uint8_t have;                        /* bit i: record i of 106, 107, 108, 187 found with its length */
    uint8_t wrong_len;                   /* bit i: found, but only with another length (not used) */
    uint8_t ok187;                       /* 187's inner CRC holds (the last 187) */
    uint8_t read_err;                    /* a flash read failed: the scan stopped there */
};
#define BLE_VM_ALL 0x0Fu

/* one valid record of the log, in order (the console's dump) */
typedef void (*ble_vm_visit_fn)(void *ctx, uint32_t off, uint32_t id, uint32_t len);

BLE_API uint32_t ble_crc16_xmodem(uint32_t crc, const uint8_t *p, uint32_t n);
/* candidate i (0 .. BLE_VM_NCAND - 1, in the order they are tried): its flash offset, *size its size; 0 past the end */
BLE_API uint32_t ble_vm_cand(uint32_t i, uint32_t *size);
/* the live area's flash offset (0 = none: no candidate with the magic and a valid first record) and, when found,
 * its size in *size (may be 0) */
BLE_API uint32_t ble_vm_live(ble_vm_read_fn rd, void *ctx, uint32_t *size);
/* walk the live area: fills info and t (the last valid 106 / 107 / 108 / 187; untouched where absent) and calls
 * visit (may be 0) per valid record -> 1 when the set is complete (BLE_VM_ALL and 187's inner CRC) */
BLE_API int ble_vm_scan(ble_vm_read_fn rd, void *ctx, struct ble_vm_info *info, struct ble_rf_trims *t,
                        ble_vm_visit_fn visit, void *vctx);

/* ---- the copy kept in Optimist's own store. Its first byte names the area it was taken from: 0xAA 0x093000,
 * 0xAB 0x095000 (as before), 0xAC 0x0E8000, 0xAD 0x0E7000 (the candidates' marks) */
enum { BLE_RF_COPY_SIZE = 100, BLE_RF_COPY_MARK = 0xB1 };
/* the area a copy's mark names (0: not a mark) */
BLE_API uint32_t ble_rf_copy_area(uint8_t mark);
enum { BLE_RF_NONE = 0, BLE_RF_FROM_VM = 1, BLE_RF_FROM_COPY = 2 };
BLE_API int ble_rf_copy_ok(const uint8_t copy[BLE_RF_COPY_SIZE]);
BLE_API void ble_rf_copy_make(uint8_t copy[BLE_RF_COPY_SIZE], const struct ble_rf_trims *t, uint32_t area);
BLE_API void ble_rf_copy_get(const uint8_t copy[BLE_RF_COPY_SIZE], struct ble_rf_trims *t);
/* the precedence of §15.4: VM (complete) -> the copy (valid) -> none. *use gets the trims chosen; *save = 1 when
 * the copy must be (re)written from the VM (absent, damaged or different) -> BLE_RF_NONE / _FROM_VM / _FROM_COPY */
BLE_API int ble_rf_choose(int vm_complete, const struct ble_rf_trims *vm, uint32_t vm_area,
                          uint8_t copy[BLE_RF_COPY_SIZE], struct ble_rf_trims *use, int *save);
/* at boot (midi_ble.c ble_midi_init): start the radio now? Only with BLUETOOTH ON saved, the boot guard counting no
 * failed start-up (boot_failed: system/bootguard.h's count of warm resets within a boot's first 30 s, a watchdog
 * reset after a radio start-up that hung among them; a power-on clears it) and the trims there (have_trims) -> 1 */
BLE_API int ble_boot_radio(int on, uint32_t boot_failed, int have_trims);

#endif
