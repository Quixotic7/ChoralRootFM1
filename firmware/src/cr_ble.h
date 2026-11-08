/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
 * Adapted from Melodee's firmware/src/midi_ble.h (3fe570d): Copyright (C) 2026 Kerem Kilic (Ellic Studio). */
/* Bluetooth LE MIDI, the FM-1 as a peripheral (HISTORY 3y236; docs/BLE-LINK.md steps 2..4, docs/ble/): Optimist's
 * bare-metal stack (firmware/src/ble/, w0ts/optimist 498a3ae4, as Melodee 3fe570d carries it) and its WL82 driver
 * (hal/fm1_ble.h, fm1_ble_rf.h, fm1_rf_cal.h). The owner's choices (docs/BLE-LINK.md section 8): the FM-1 only (the
 * SMK-37 builds without it), the peripheral (a Mac, iPad or phone connects to "ChoralRoot XXXX"; no scanning), LE
 * legacy pairing Just Works with a bond, the stock-tested 27-octet link-layer packets, MIDI only (channel messages,
 * clock and transport; no SysEx), Off by default, saved, the radio started only at power-on (3y238). The radio's start-up
 * program lives in the resource region (cr_res.h: BLE_RF_RES -> .res.ble). The glue: cr_ble.c. */
#pragma once
#define BLE_API static
#define BLE_DEVICE_NAME "ChoralRoot"    /* advertised as "ChoralRoot XXXX" (BLE_NAME_HEX: the address's last digits) */
#define BLE_CENTRAL 0                   /* the peripheral only (connecting out: docs/BLE-LINK.md step 6) */
#define FELUCCA_BLE_BOND 1              /* LE legacy pairing, Just Works, a bond kept (ble_cfg.h: LL encryption on) */
#ifndef BLE_DIAG
#define BLE_DIAG 0                      /* BLE_DIAG=1 ./build.sh: the driver's counters in 81 BLE (3y241) */
#endif
#ifndef BLE_DMA_MEMORY                  /* (tests/cr_ble_test.c: plain memory on the host) */
#define BLE_DMA_MEMORY __attribute__((section(".pool"), aligned(4)))   /* the baseband's block: SRAM, as Melodee's */
#define FM1_RF_CAL_MEMORY __attribute__((section(".pool")))
#define BLE_RF_RES __attribute__((section(".res.ble")))   /* hal/ble_rf_tables.h's program: in the resource region */
#endif
#define BLE_LL_MAX_OCTETS 27u           /* the stock-tested PDU size (docs/BLE-LINK.md decision 9) */
#define BLE_BARRIER() RING_PUBLISH()
#include "ble/ble.h"
#include "ble/ble_vm.h"

/* what the rest of the firmware calls (cr_ble.c) */
static int cr_ble_emit(uint32_t pkt);            /* the audio ISR (usb.c midi_out_event): a packet for the peer */
static void cr_ble_drain(void);                  /* TIMER5: the peer's packets into the MIDI input queue */
static void cr_ble_boot(void);                   /* power-on, before the audio and core 1: the record, the trims */
static void cr_ble_start_boot(void);             /* power-on, the last step before the interrupts: the radio */
static void cr_ble_poll(void);                   /* the main loop: a record to save */
static int cr_ble_running(void);                 /* the radio started this power-on */
static int cr_ble_will_start(void);              /* after cr_ble_boot: the radio starts this boot */
static void cr_ble_supplies(int ok);             /* main.c: stock's supplies applied (1) or a P33 timeout (0) */
/* the bisection's switches (3y243; BLE_DIAG builds only, else 0): 82 BLE_TEST saves a mask in the Bluetooth record,
 * applied at the next power-on (main.c): a ChoralRoot subsystem Melodee does not run is left off, one at a time */
#define CRBL_T_NOSCAN 1u                         /* no analog key scan (velocity: the rows PA7 / PA8, the PA10 probe) */
#define CRBL_T_NOAUDIO 2u                        /* no audio_init (no ALNK DMA, no audio ISR, so no core 1 jobs) */
#define CRBL_T_NOUART 4u                         /* no TRS MIDI UART */
#define CRBL_T_LATE 8u                           /* the radio started in the main loop, as Melodee's ble_boot_poll */
#define CRBL_T_NOSUPPLY 16u                      /* no stock supply sequence (the boot loader's) */
#define CRBL_T_VCO 32u                           /* the VCO band from a full sweep, the last "low" at the flip (3y247) */
#define CRBL_T_VCO_HI 64u                        /* ... the first "high" at the flip */
static uint32_t cr_ble_test(void);
static int cr_ble_set_test(uint32_t m);       /* 82 BLE_TEST (cr_backup.c): 0 saved, 1 refused */
#define CRBL_DBG_N 56u
static uint32_t cr_ble_debug(uint32_t v[CRBL_DBG_N]);   /* 81 BLE (cr_backup.c): the radio's state, fm1_install --debug */
static int cr_ble_set(int on);                   /* Options > Bluetooth: 0 refused (no region, no calibration) */
static int cr_ble_on(void);
static void cr_ble_forget(void);                 /* Options > Forget Bluetooth */
static void cr_ble_status(char *d, uint32_t n);  /* the Options line under Bluetooth */
