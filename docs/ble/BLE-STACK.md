# BLE MIDI: our own stack (route C)

How Bluetooth LE MIDI works in Optimist today: the link layer, host, BLE-MIDI, the driver for the FM-1's radio, the radio's
start-up and stored trims, the central role, the console tools. Registers and engine rules are in
[BLE-HW-FACTS.md](BLE-HW-FACTS.md) ("HW §n"); the devices list, LAST and the pairing UX are in
[BLE-DEVICES-DESIGN.md](BLE-DEVICES-DESIGN.md).

## 1. What it is, and where it stands

- Builder items (MIDI & USB group): **`BLE`** (`FELUCCA_BLE`, off by default; off, the image is byte-identical but for the
  builder's configuration hash), **`BLE_DIAG`** (the `blell` diagnostics), **`BLE_BOND`** (pairing, an encrypted link),
  **`BLE_CENTRAL`** (the devices list, connecting out, reconnecting LAST; at level 1 it brings the bonding code with it). Their
  costs: `tools/builder/costs.json` (docs/BUILDER.md). No V15 file is needed: the radio's start-up tables are committed (section 8.3).
- **SYSTEM > BT MIDI > MODE is OFF by default** and then the radio does not even start; MAN or AUTO switch it on.
- **Verified on an FM-1** (2026-10): a Mac (Audio MIDI Setup) connects to the FM-1 and MIDI goes both ways; the FM-1 as central
  picks a Mac from the devices list, pairs (a passkey on the FM-1's screen) and reconnects after a power cycle; an iPhone app
  connected to the FM-1 as central. Unconfirmed on hardware: the FM-1 as central to an iPhone with the passkey. Not tried:
  Windows. Unmeasured: the radio's analog side (TX power, sensitivity, drift; HW §12).
- Provenance (clean room): written from the Bluetooth Core Specification v5.x (LE 1M only) and the MIDI Association's "MIDI
  over Bluetooth Low Energy" 1.0; the driver from the fact sheet and the Core spec. No vendor code, libraries, IR or
  disassembly were read for the stack or the driver (the fact sheet was read from the vendor's binary by a separate reader).
  Portable C, no `malloc`, no C library.

## 2. Layout (`firmware/src/ble/`, `io/midi/`)

`ble_cfg.h` (every setting) · `ble_prim.c` (CRC24, whitening, CSA #1, AA rules) · `ble_aes.c` (AES-128, the LL's AES-CCM, RPA `ah`)
· `ble_hw.h` (**the driver interface**, section 3) · `ble_ll.c` + `ble_ll_central.c` (link layer; the scanner, initiator and
master) · `ble_host.c` (GAP data, L2CAP, signalling) · `ble_att.c` (ATT server, the static GATT database) · `ble_smp.c` +
`ble_smp_init.c` (SMP responder, initiator) · `ble_gattc.c` + `ble_central.c` (ATT client; the host side of a connection out) ·
`ble_midi.c` (BLE-MIDI packets to and from USB-MIDI event packets) · `ble_store.c`, `ble_scan.c` (the one remembered device; the
nearby devices' table) · `ble_hw_wl82.c` + `ble_hw_wl82_central.c` + `hal/fm1_ble.h` (**the driver** on the AC791N / WL82 engine,
section 7) · `ble_hw_stub.c` (a driver that never calls back, `FELUCCA_BLE_STUB=1`) · `ble_vm.c`, `hal/fm1_ble_rf.h`,
`hal/ble_rf_tables_v15.h` (stored trims, the start-up program runner, the captured tables; section 8) · `ble_diag.c` (`blell`) ·
`ble_stack.c` (the unity-build wrapper: every function `static` via `BLE_API`) · `io/midi/midi_ble.c`, `ble_devices.c`,
`ble_connect.c` (rings, bond, address, on / off; the devices list; connecting out and LAST) · `tools/ble_rf_capture.py`,
`ble_rf_pack.py`, `ble_vm.py` (the table capture, its packing, a VM dump decoder).

## 3. The driver interface (`ble_hw.h`)

Built for an engine that does each event's timing in hardware: advertising on 37 / 38 / 39 with an automatic SCAN_RSP, anchors and
the transmit window, CSA #1 hopping, the access-address match, CRC (software gives the init), SN / NESN with acknowledgement and
retransmission from two alternating TX buffers, the event counter and the instant compare.

**One context.** Every `ble_ll_hw_*` call comes from the driver's BLE interrupts (event IRQ and RX IRQ at one priority, never
nested); the link layer calls `ble_hw_*` only from inside them (and `ble_hw_adv_start` once at start-up, before they are on). The
host and the BLE-MIDI pump run there too; the rest of the firmware talks to it through single-producer rings only.

The link layer asks: `ble_hw_adv_start` / `_stop`, `ble_hw_conn_start` (called inside `ble_ll_hw_connect_ind`: leave
advertising, listen in the transmit window), `ble_hw_conn_update` and `_chmap_update` (the engine switches at the instant),
`_set_lengths`, `_tx_kick`, `_conn_stop`, `_addr`, `_time_us`, `_rand`; with `BLE_CENTRAL` also `ble_hw_scan_start` / `_stop` and
`ble_hw_init_start` / `_stop`. The driver reports: `ble_ll_hw_connect_ind` (1 when taken), `_rx` (a new data PDU: CRC good, not a
retransmission), `_tx` (a TX buffer is free: the LL writes the final, already encrypted PDU, or returns 0 for an empty one),
`_tx_acked`, `_event_end(counter, rx_ok)` (every event, also with nothing received), and with `BLE_CENTRAL` `_adv_report` and
`_master_start`. PDUs cross as on air without AA and CRC: 2-octet header + payload (up to 251, + 4 MIC when encrypted).

## 4. Link layer (peripheral)

- **Advertising:** ADV_IND = AdvA + flags (`02 01 06`) + the BLE-MIDI 128-bit UUID, ChSel 0; SCAN_RSP = the complete local name
  `Optimist XXXX` (`BLE_DEVICE_NAME`; stock's was `FM-1_BLE`). 100 ms, 37 / 38 / 39.
- **CONNECT_IND checks:** our AdvA and RxAdd; Interval 6..3200; Timeout 10..3200 and > (1 + Latency) x Interval x 2; WinSize
  1..min(8, Interval - 1); WinOffset <= Interval; Hop 5..16; >= 2 channels; not the advertising AA. Anything else is ignored and
  advertising goes on (`cind_rej_why` says which).
- **Timers:** 0x3E without a packet in the first window + 6 intervals; supervision timeout 0x08; 40 s LL response timeout 0x22.
- **Procedures:** version and feature exchange (we start LL_PERIPHERAL_FEATURE_REQ after 6 events if the central has not); data
  length; connection update and channel map with instants (an instant already passed ends the link, 0x28);
  LL_CONNECTION_PARAM_REQ accepted when valid, else 0x1E; PHY_REQ answered 1M / 1M (anything else ends the link, 0x1A); ping;
  termination both ways; LL_ENC_REQ refused (0x1A) unless `BLE_LL_ENC=1`; LL_UNKNOWN_RSP for the rest. Version 9 (5.0), company
  0xFFFF. Control PDUs go first; L2CAP frames wait whole in a 1 KiB ring and are cut to the data length when the engine pulls them.

## 5. Host and GATT

Handles: 1 GAP 0x1800; 2-3 Device Name (`Optimist XXXX`); 4-5 Appearance (0); 6-7 Peripheral Preferred Connection Parameters
(6, 9, 0, 100); 8 GATT 0x1801; 9-11 Service Changed + CCCD; 12 BLE-MIDI service `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`; 13-15 MIDI
I/O `7772E5DB-3868-4112-A1A9-F2669D106BF3` (read: empty; write without response; notify; Write Request accepted too) + CCCD.

- ATT: MTU exchange (ours 247 = one 251-octet LL PDU), Find Information, Find By Type Value, Read By Type, Read, Read Blob, Read By
  Group Type, Write Request / Command, Handle Value Confirmation; Request Not Supported for the rest. No attribute needs
  security unless `BLE_MIDI_NEED_ENC`.
- **Service Changed is never indicated** (`BLE_SC_ON_SUBSCRIBE=0`). Indicating it made the Mac confirm and then never discover
  the MIDI characteristic (blell10-12, 2026-10-09); our database never changes while we run (Apple's Accessory Design Guidelines
  58.12.2). Handles 1..15 stay. If the layout ever changes, add the Database Hash (0x2B2A) instead.
- When a client turns the MIDI notifications on and the interval is outside 6..9, an L2CAP Connection Parameter Update Request
  {6, 9, 0, 100} goes out; after a reject, once more {12, 12, 0, 100} (stock's values and order). Command Reject to every other
  signalling request.

### 5.1 Security (`ble_smp.c`, `ble_aes.c`)

- **Default** (`BLE_SMP_LEGACY=0`): a Pairing Request gets Pairing Failed (Not Supported); LL_ENC_REQ is refused. A Mac connects and
  plays MIDI like this.
- **`BLE_SMP_LEGACY=1`** (needs `BLE_LL_ENC=1`; `BLE_BOND` and `BLE_CENTRAL` set both): LE legacy pairing, Just Works as responder
  (NoInputNoOutput, TK = 0; a Mac asking for Secure Connections still ends in legacy). The central starts the LL encryption with
  the STK (EDIV 0, Rand 0); then our LTK / EDIV / Rand and, when asked, our identity (our IRK and static address), then the
  central's keys (its IRK and identity address go to the firmware, `ble_app_peer_id`, RAM only; its LTK is not kept: a returning
  central is found by EDIV / Rand). **Our IRK is fixed, made from our address** (`smp_own_irk`, both roles): macOS refuses a new IRK
  from a device it already paired with, so a random one per pairing made the Mac ask for a passkey at every power-on (2026-10-18).
  The bond (one key slot, `ble_host_set_key`) goes to the firmware (`ble_app_bond`); `midi_ble.c` keeps it in the settings record
  (`persist_t.ble_bond`, 28 B; never in the VM area) and **saves it at once** unless a song is playing (then once quiet).
  Not done as responder: the 30 s SMP timeout, Secure Connections, signing, LL_PAUSE_ENC.
- **`BLE_SMP_SEC_REQ=1`**: an SMP Security Request at every connection start, as stock V15 (Apple's guidelines, 58.10, advise
  against it). **`BLE_MIDI_NEED_ENC=1`**: the MIDI I/O value and CCCD need an encrypted link (Insufficient Authentication 0x05 until
  then, the way 58.10 asks a peripheral to start pairing). Pairing is offered, not forced: Apple does not require it to connect.
- Builds: `OPTIMIST_BLE_SMP=1` (pairing), `=2` (+ Security Request), `=3` (+ MIDI needs encryption) in the environment of
  `tools/optimist.py build --set BLE=1` (not `FELUCCA_*`, which the builder strips).
- **Cost on the interrupts:** the LL encrypts a PDU when the TX service loads it (end of the RX interrupt) and decrypts in the RX
  interrupt, in software (`ble_aes128`): CCM takes 3 + 2 x ceil(n / 16) AES blocks, 7 for a MIDI notification, 35 for a full PDU;
  empty PDUs are not encrypted. A block takes 169 ns on the host; on the FM-1 it is unmeasured (`isr_max_us` with `enc_on` 1).

## 6. BLE-MIDI and the firmware (`ble_midi.c`, `io/midi/midi_ble.c`)

### 6.1 Packets and the router
- **Out:** one notification per connection event (up to `BLE_MIDI_PER_EVENT` = 2 with a backlog), as many events as fit in MTU - 3.
  Real 13-bit millisecond timestamps (stock sends `80 80`); running status inside a packet; SysEx over packets.
- **In:** decoded to USB-MIDI event packets with timestamps: running status with or without a timestamp, low-part wrap, real
  time anywhere, SysEx over packets, system common. A message cut at a packet's end is dropped.
- **Router:** channel messages, and the clock and transport for `clock_sync.c` (CLK IN BLE); the BLE interrupt puts them in `ble_in_q`,
  the TIMER5 tick moves them into `midi_in_q`, where they play as USB's, each timed by its arrival less its distance to the
  packet's newest timestamp. `midi_out_event` (the audio ISR; the FM-1's own notes) also
  queues into `ble_out_q` while a client listens, and CLK OUT's clock goes out timed (`ble_rt_push`). Nothing is bridged between USB / TRS and BLE. `ble_midi_route`: bit 0 in,
  bit 1 out.
- **No stuck notes:** notes a central had on (`ble_held`, 256 B) are ended with a note-off through the same ring when the link goes
  (any cause) and when BLUETOOTH goes OFF; notes sent after OFF are dropped; USB and TRS notes keep sounding.

### 6.2 On / off, boot, address
- **The switch:** SYSTEM > BT MIDI > MODE (OFF, or MAN / AUTO: on; `ui/optimist/op_bt.c`). **Bit 24** of the settings word (`SETTINGS_BLE_ON_BIT`, 1 = ON; bit 23 is
  reserved, 25 is KNOB COLORS), saved with the settings. A fresh unit, an older record and one SLOOP 2.3 / 2.4 wrote read OFF; a
  build without BLE keeps the bit as read.
- **Boot** (`system/main.c`): the settings are read first. `ble_midi_init` scans the VM (flash reads only), chooses the trims and
  attaches the two BLE vectors masked. **Only when ON was saved** does it go on: the stack, the address, `rf_init` and the
  baseband (`ble_radio_start`), advertising, the interrupts let go. With OFF no RF, BT or baseband register is written, so a radio
  start-up that hangs cannot stop the FM-1 booting. In DUAL builds (the default) this runs before `audio_init` as in stock V15
  (docs/DUAL-CORE.md section 3: the second core spins in its RAM loop, its jobs start 400 ms after BLE runs); otherwise after
  `usb_start`. When the menu switches ON at run time the radio starts then, once per boot. **Without stored trims it never
  starts:** the switch only changes the setting and the row shows NO CAL.
- **Safety net** (`ble_boot_radio`, `bootguard`): a boot the guard counts as following a failed start-up (a warm reset in a boot's
  first 30 s, e.g. a watchdog reset after a radio start-up that hung; that a hang ends in the watchdog's reset is [I]) leaves
  the radio off for that boot with ON kept saved, so the menu can switch it OFF. A power-off / on clears the count; two failed
  start-ups in a row go to USB rescue. `bletrim` shows `boot_failed`.
- **OFF while advertising:** `ble_hw_adv_stop`. **OFF while connected:** `LL_TERMINATE_IND` (0x13); the link closes when the central
  acknowledges (or on the supervision timeout) and the layer does not advertise after it. **ON:** advertising again.
- **Address:** a random static address made once from the random source and kept (`persist_t.ble_addr`, saved once quiet). The
  VM's record 104 is not used (V15 writes none).
- **Settings record tail** (`storage/project.c persist_t`): `ble_addr` 8 B, `ble_rf` 100 B (section 8.2), `ble_bond` 28 B, `ble_dev`
  70 B (the device store). A build without BLE reads the rest as its own and drops these when it saves.

## 7. The baseband driver (`ble_hw_wl82.c`, `hal/fm1_ble.h`)

Every register write in `hal/fm1_ble.h` and every control-block write in the driver names its HW section; register accesses live in
`hal/` (the build's register check). One static 1,508-byte RAM block (HW §4); PDUs are read and written in place.

- **Advertising** (HW §6, stock's order). **CONNECT_IND** (HW §7): in the RX interrupt, `ble_ll_hw_connect_ind` checks it and calls
  `ble_hw_conn_start` before returning; one it refuses restarts advertising.
- **Per event** (HW §8): IRQ 29 delivers new packets in the engine's buffer order with **a software SN check**, re-arms the buffer,
  then runs the TX service (HW §8.2). The event interrupt never touches TX. IRQ 45 takes a pending reception first, reads the
  counter (column 3 - 1), applies the instants (HW §9; one seen late is applied at once and counted), narrows the receive window
  after the first packet of a new anchor and calls `ble_ll_hw_event_end`.
- **Time base** TIMER4 (24 MHz) extended to 32-bit microseconds; the slot clock steps backwards (HW §2.3). **Random** `0x13B00/04`.
- **Interrupts:** IRQ 45 and 29 at **priority 2** on CPU 0, below the audio (IRQ 11) and TIMER5 (63), never nested in each other. The
  engine does every radio deadline in hardware; software's part is long (state 7 within the transmit window, a TX refill and the
  instant writes within one interval, >= 7.5 ms), so BLE waits for an audio half and the audio never waits for more than one BLE
  handler. The handlers run from flash; every flash write disables all interrupts, during which the engine runs on alone (missed
  events are the central's retransmissions). In the emulator a handler takes at most 36 us at 96 MHz; on the FM-1 it is unmeasured.
- **Link stops** (`hw_stop`): before a link is programmed again the driver waits for `0x28038` bit1 (engine idle), bounded by TIMER4
  (40 ms on the main loop's stops, 3 ms on a connection's end, 0.2 ms before a link opens) and by a read count, so a TIMER4 that
  does not count cannot hold it.

## 8. The radio's start-up and the stored trims

### 8.1 `fm1_ble_rf_init` (`hal/fm1_ble_rf.h`)
Runs `fm1_ble_rf_run` (the captured program), then HW §5.1 steps 3-4 and §5.4. The program: 522 register writes, 1,179 BBP window
writes and 18 reads, 26 direct BBP registers, 512 RF-die LUT words, 101 trim marks, 4 delays, 1 replay (group 10's reload of group 4's block), one VCO scan,
10 section markers; the window-D read-back loop is left out. The clock words of group 1 are not replayed (`0x10010` bit 10 is our UART's clock, `0x10008` bit 3 the second
core's start). It is stored packed (format 3, `tools/ble_rf_pack.py`: `OP_LUT2` LUT words as small deltas, `OP_WWG` repeated
window-write groups), 10,706 B to 5,795 B; the pinned SHA-256 is over the unpacked format-2 program, which every BLE build checks
(`ble_rf_capture.committed_reason`).

- **Trims:** the stored values go into the writes that carry them (HW §14.5, §16.4); the capture finds which write carries which
  field by perturbing each trim byte.
- **VCO scan** (group 7) is ours: with stock's fine code, a bisection over bands 0-63 in `0x11938` [25:19] using the strobes of
  `0x11978` and its comparator (bit 17 low means a higher band, bit 18 a lower one; the meaning is [I]). The result is kept for
  the console; stock's final band and fine code follow as captured.
- **Group 5** (the crystal ramp of `0x11930`) is decoded first and written in one burst from RAM with interrupts off
  (`fm1_rf_burst_run`; `tools/build.py` refuses a build where it is not in `.ram_text`). The FM-1 hung inside that ramp in builds
  where the table sat at a certain flash alignment (2026-10-08); the best explanation [I, unmeasured] is a flash fetch mid-ramp.
- **Left out** (TODO hardware): the window-D read-back loop (HW §16.2); the BT TX trims keep stock's first-written values.
- **Section markers:** the capture marks where each HW §16.1 group starts; `fm1_ble_rf_init` adds 14 (BT block) and 15 (done).
  `bletrim` prints the last entered (`rf_section`) and the set (`rf_sections`; complete: 15 and `0000EDFC`).

### 8.2 The stored trims (`ble_vm.c`)
- **Where:** `0x0E8000` on an FM-1 (HW §14.1). The reader tries `0x0E8000` (4 KiB), `0x0E7000` (a candidate only), `0x093000`, `0x095000`
  (the emulator's layout, 8 KiB). A candidate is the live VM only if it starts `55 AA AA 55` **and** its first record passes its
  check; the magic alone is not enough (`0x095000` is UP_FM6's voices now, an object of ours, never a VM). Then the records to the
  first failing check; the last valid 106 / 107 / 108 / 187 win; 187's inner CRC is checked. The reader has no write path.
- **The copy:** 100 bytes (`persist_t.ble_rf`: a mark with the source area, `0xAC` `0x0E8000`, `0xAD` `0x0E7000`, `0xAA` `0x093000`,
  `0xAB` `0x095000`; the four records; CRC-16/XMODEM), written with the settings once quiet when the VM has a complete set the
  copy does not hold.
- **Precedence:** the VM's complete set, the copy, none. With none the radio is never started and the console says why. BLE writes
  nothing in `0x0E7000-0x0E9FFF`; Optimist's flash map keeps off it (HW §15).

### 8.3 The capture and the build
`tools/ble_rf_capture.py` (HW §17) runs the user's stock `FM-1.fwsc` in the emulator and writes the tables; the result is committed
as `firmware/hal/ble_rf_tables_v15.h` (format 3, 39.9 KB, SHA-256 `30ba97ea…`). `tools/build.py` puts the tables at
`build/gen/ble_rf_tables.h`: a matching local cache in `config/ble/` (git-ignored) as it is; with `FM1_STOCK_FWSC` and no matching
cache a capture from that file (it must still be stock V15, else the build stops); with neither, the committed tables. All three
give the same content, so a BLE build needs no V15 file (BUILDING.md). A capture takes about 30 s.

## 9. The central role: connecting out (`BLE_CENTRAL`)

From the Core spec, the BLE-MIDI spec and HW §21. One link at a time, time-sliced with advertising.

### 9.1 Link layer and driver (`ble_ll_central.c`, `ble_hw_wl82_central.c`)
- **Scanning:** state 1, interval 64 / window 60 slots (40 / 37.5 ms), active (the engine sends SCAN_REQ with the Core's backoff) for
  the names controllers put in their scan response. A ring of 16 raw reports from the RX interrupt to the main loop. Only while
  the devices list is open (advertising pauses meanwhile).
- **Our CONNECT_IND** (`ble_ll_connect`): random AA by the Core rules, a **random CRCInit** (not the vendor's fixed one), WinSize 2,
  WinOffset random in [Interval / 2, Interval - 1], Hop random 5..16, 37 channels, latency 0, timeout 2 s, SCA 0, **interval 9
  (11.25 ms)**: the top of what our peripheral asks for, about 89 packets a second each way (7.5 ms would add a third more
  interrupts while the software AES-CCM cost is unmeasured). Values: `BLE_CENTRAL_*` in `ble_cfg.h`.
- **State 3 and the switch to master** follow HW §21.3. The first anchor is shortened by the slots the set-up came after the hit
  (`hw_first_anchor`: the event interrupt can come late). No column read (op 2) on the initiating path.
- **The master's events** run on the peripheral's code (HW §8, §8.2) with the differences of HW §21.4. We start version, feature
  and length exchange (the length before any encryption) and the encryption the host asks (LL_ENC_REQ: Rand, EDIV, SKDm, IVm); we
  answer the peripheral's feature exchange, PHY request, LL_CONNECTION_PARAM_REQ and L2CAP update request (with an
  LL_CONNECTION_UPDATE_IND, instant counter + 8..11), and can send LL_CHANNEL_MAP_IND (unused, AFH is off). The LL's **encryption
  pause** (LL_PAUSE_ENC_REQ / RSP, then LL_ENC_REQ with the new key) re-keys an encrypted link. A master that hears nothing in six
  intervals closes with 0x3E.

### 9.2 GATT client and security (`ble_gattc.c`, `ble_central.c`, `ble_smp_init.c`)
- **GATT client:** MTU 247, Find By Type Value for the BLE-MIDI service (else Read By Group Type), Read By Type 0x2803 for the MIDI
  I/O characteristic, Find Information for its CCCD, Write Request 0x0001: **ready**. Notifications are MIDI in (section 6.1);
  MIDI out is **Write Without Response** from the same encoder and ring. One request outstanding, 30 s timeout.
- **Levels** (`BLE_CENTRAL` 1 / 3, `ble_cfg.h` `BLE_CEN_PAIR`; BLE-DEVICES-DESIGN.md 4.5): what follows is
  level 1. Level 3 has no SMP initiator and no LL encryption as master (a Security Request gets Pairing Not Supported, any
  request for security `NEED_PAIR`).
- **Security, only when the peer asks:** a bonded LAST is encrypted with its LTK right after the connection; an Insufficient
  Authentication / Encryption / Key Size error or the peripheral's Security Request encrypts with the bond, else **pairs as
  initiator**. Our Pairing Request is always **DisplayOnly + bonding + MITM**, legacy: the responder's IO capability picks. A
  responder that can type (a Mac, a phone) gets a **passkey** (0..999,999 from the hardware RNG; the FM-1 shows it, the user types
  it there; the keys are authenticated, `BLE_KEYS_AUTH`); one that cannot (another FM-1, most controllers) gets **Just Works**,
  silent on both screens, unless the host knows the peer needs MITM (then it fails with Authentication Requirements 0x03). The
  passkey is asked for in the first pairing (macOS drops an alert that follows another). An older Just Works bond the peer
  refuses with 0x05 is paired again with MITM **on the same link** through the encryption pause; a peer that refuses that before
  any passkey is shown gets `BLE_CF_NEED_MITM`: the link is left and made again once with the passkey from the start. Keys both
  ways: its LTK / EDIV / Rand and IRK + identity address go to the firmware (`ble_app_central_keys`); ours are our fixed IRK and
  a throwaway LTK. The 30 s SMP timer restarts with each command of ours (the user types within it). Secure Connections is not
  implemented (it needs P-256; the next step if iOS wants it for an app's characteristic).
- **Endings** (`ble_central_fail`): LOST (the LL reason), NO MIDI SERVICE, PAIRING (the SMP reason), AUTH, NEED_MITM, GATT ERROR,
  KEY (it lost our bond: dropped after three tries). A failure leaves the link once the queued PDUs went.
- **RPA:** `ble_rpa_resolve(irk, addr)` = ah (Core Vol 3 Part H 2.2.2) with the software AES, checked against the Core sample.
- **Roles and MIDI:** the router is unchanged; `ble_midi_ready()` means "its notifications are on" as central and "the central
  subscribed" as peripheral. While a central link is up the FM-1 does not advertise; when it ends, advertising (or the devices
  scan) comes back.

Hardware (2026-10-09, an iPhone app advertising BLE-MIDI): the engine sent our CONNECT_IND by itself, the peer answered every
event, LL encryption as master worked, Just Works completed and encrypted, discovery found the service. The iPhone then refused
the CCCD write with 0x05 even after Just Works (it wants an authenticated key): hence the passkey path. The firmware side (a pick,
LAST and its search, retries, what is shown) is in BLE-DEVICES-DESIGN.md.

## 10. Tests (`tests/run_tests.sh`; ASan + UBSan where the compiler has them)

Host: `ble_prim_test` (AES, CCM with the Core's samples, CRC24, whitening, CSA #1, c1 / s1), `ble_stack_test` (a simulated central
through a fake `ble_hw`; also with `BLE_LL_ENC` and `BLE_SMP_LEGACY`), `ble_driver_test` + `tests/ble_fake/` (the WL82 driver against
a fake engine built from the FM-1's measurements: the HW §8.2 TX contract, RX by RXTOG, loss, a clock stepping back, TIMER4 wrap; a
Mac-like central), `ble_scan_test`, `ble_scan_driver_test`, `ble_central_test` (the central against a simulated peripheral with every
pairing failure, the passkey, bonds), `ble_central_driver_test`, `ble_f2f_test` (two whole stacks), `ble_store_test`, `ble_midi_test`,
`ble_midi_in_test`, `ble_vm_test` (HW §14 on built images and the real extract in `docs/ble-traces/v15-vm-trim-map.txt`; the boot-guard
decision), `ble_rf_capture_test.py`, `ui_optimist_ble.h` (inside `ui_optimist_test.c`: the SYSTEM lines and the list). Emulator
(`fm1-emulator` `feat/ble-engine`): `ble_emu_test.py` (stock V15's start-up reproduced write for write, a virtual central, the
radio off by default), `ble_emu_central_test.py` (virtual BLE-MIDI peripherals, iPhone-like with the passkey and Mac-like with an
RPA; a reboot, LAST reconnects), `ble_emu_boot_test.py` (the full option set boots with BLUETOOTH saved ON). The emulator models the
engine from the fact sheet and stock V15's behaviour: it proves the software, not the radio.

## 11. On a real FM-1: the console and the breadcrumb

Needs a console build (`USB_CONSOLE=1`; SYSTEM > USB > SERIAL ON, then a restart). The commands only read (flash over SPI, RAM); none
writes memory, flash or a register.

| Command | Prints |
|---|---|
| `blevm` | every VM candidate's first word, every valid record (offset, id, length), the live area, which records are there, 187's CRC, the data |
| `blevmdump` | every candidate area raw (24 KiB in `flr`'s format, then `end`); `tools/ble_vm.py LOG` decodes a saved log |
| `bletrim` | the trim source, BLUETOOTH and whether the radio started this boot, the VM summary, the copy raw and decoded, the tables' SHA-256, what the start-up did (ops, trims, LUT words, timeouts, the scan's band, the last section and the set) |
| `blell [clear\|regs]` | (`BLE_DIAG`) the counters and rings of `ble_diag.h`; `regs` adds the engine's columns and interrupt registers |
| `flr OFFSET [LEN<=256]` | raw flash from `0x093000` up (the VM: `flr 0xe8000 256`) |
| `dbg` | ends with `prev_ble` / `prev_ble_irqs` (the breadcrumb) |

**`BLE_DIAG`** builds the counters: the LL, the driver, the TX / RX rings, the protocol ring of the last 64 PDUs both ways (every ATT,
signalling and SMP PDU, and LL encryption PDUs), BLE-MIDI in, and for the central role `scan_*`, `cen_*`, `m_*`, `gc_*`, `si_*`,
`rc_*`. Off, every recording is `BLE_DG(statement)`, which compiles to nothing; the radio code, `bletrim`, `blevm`, `blevmdump` and the
breadcrumb are the same. `blell` reads RAM only; counters run from the radio's start or `blell clear`. The field names are in
`ble_diag.c`; the ones to read first: `adv_events` (IRQ 45 while advertising: must keep growing), `cind_ok` / `cind_rej_why`
(why a CONNECT_IND was refused: 1 not advertising, 2 format, 3 RxAdd, 4 AdvA, 5 interval / latency / timeout, 6 WinSize, 7 WinOffset,
8 hop, 9 map, 10 AA), `rx_good` / `rx_crc_bad` / `rx_repeat` / `rx_desync`, `tx_queued` / `tx_acked` (should follow each other) /
`tx_ack_evt_max` (1-3), `ctl_rx_last` / `ctl_tx_last` / `att_rx_last` (the last 8 opcodes), `closes` / `close_reason` / `close_by`
(0 us, 1 the central, 2 supervision timeout, 3 never established, 4 procedure timeout, 5 protocol error, 6 our terminate
unacknowledged), `isr_max_us`, `clk_step_max`.

**The BLE breadcrumb** (`hal/fm1_ble_rf.h`): a word in `.noinit` (it survives a watchdog reset, not a power cycle) that every step of the
radio's start-up writes, and a count of BLE interrupts since. At boot `main.c` moves them to `prev_ble` / `prev_ble_irqs`. `prev_ble` reads
`B1SSGGRR`: `SS` the step, `GG` rf_ops / 256 at the last group, `RR` that group (15 = rf_init done). Steps: 01 the menu's ON taken, 02
`ble_init`, 03 `rf_init`, 04 the baseband block, 05 started, 06 `ble_enable`, 07 stopping the link, 08 control block reset, 09 advertising
programmed, 0A advertising started, 0B interrupts about to be let go, 0C running, 0D OFF done. `prev_ble_op` / `_gop` / `_addr` name the
last access begun (a register address, `0xBB00rrdd` a BBP transaction, `0x5B00ccaa` an RF-die SPI command); `prev_ble_stop` the last
link stop (`5DPP0000` waiting for `0x28038` bit1, `5EPPUUUU` done, `PP` the path: 00 adv, 01 scan, 02 init, 03 conn, 04 open). A hang at
step 0C with a huge `prev_ble_irqs` is an interrupt storm. After a freeze **wait** for the watchdog, do not power-cycle, then run `dbg`
and `bletrim`. The **rescue screen** (OPTIMIST USB RESCUE, `recovery.c`) prints `BOOT SSSSPPPP FFFFFFFF` (the last start-up's stage and the
one before; 41-4B are `main.c` `BOOT_STAGE`s), `BLE <ble_step> <ble_stop>` and `CRASH CCCCVVVV <pc>`: a photo of it is the crash report.

If the radio hangs the FM-1 at a boot, wait (the watchdog restarts it, the next boot leaves the radio off, set MODE to OFF); if even that
does not come up, power on with OCT- and OCT+ held 3 s (UBOOT).

## 12. Open

- The radio's analog side on hardware: TX power, sensitivity, drift, the VM on a second unit, the window read-backs (HW §12).
- ISR durations and the cost of software AES-CCM on the FM-1. Whether the engine times 251-octet PDUs (data length extension) with
  no other setting is untested on hardware (stock never uses DLE); `BLE_LL_MAX_OCTETS=27` switches it off.
- The FM-1 as central to an iPhone with the passkey, end to end; Windows; whether iOS wants Secure Connections.
- A boot that never came back (2026-10-10, BLE + CDC + BOND + CENTRAL + DIAG, BLUETOOTH saved ON, then a rescue screen with no
  trace): not reproduced in the emulator under many conditions. The link-stop waits are now bounded by a read count as well as
  TIMER4 and the start-up steps leave the breadcrumb on the rescue screen, so the next one will say where it stopped.
- Not built: Secure Connections, signing, directed advertising, two links at once, the chip's AES block.
