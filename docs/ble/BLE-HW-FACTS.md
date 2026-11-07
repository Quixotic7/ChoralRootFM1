# BLE baseband hardware facts (AC791N / WL82)

What the driver (`firmware/src/ble/ble_hw_wl82.c`, `firmware/hal/fm1_ble.h`, `fm1_ble_rf.h`) depends on: registers,
the per-link control block, buffer rules, sequences, the stored radio trims. Facts only: no vendor code. The software
that uses them is in [BLE-STACK.md](BLE-STACK.md) and [BLE-DEVICES-DESIGN.md](BLE-DEVICES-DESIGN.md).

Section numbers are the ones code comments cite (`HW §8.2`). A section the code no longer needs is short (§12, §13, §18)
or gone (§19, §20); the numbering has gaps on purpose.

## 0. Sources, tags, caveats

Tags: **[M:t]** an emulator MMIO trace of a reference firmware; **[M:d]** a RAM or flash image after it ran; **[M:hw]** measured on
a real FM-1 (Optimist's own driver and console); **[M:s]** / **[IR]** read from the vendor binary, its debug info, or what its
code does; **[I]** inferred.

Reference firmwares: stock V15 (`FM-1.fwsc`, SHA-256 `db1642b2…`, advertises as `FM-1_BLE`) and the SDK's `demo_ble` (BLE only), both
run in the emulator `fm1-emulator`. It stores configuration words and models only the BT slot clock and alarms, the PLL
comparator and the filter-calibration result: every value a firmware **writes** there is genuine, every value it **reads** from
the radio is a model output. Connection facts (§7-§9) come from the vendor binary and, since the driver runs, from the FM-1.

## 1. Memory map

| Range | What | Tag |
|---|---|---|
| `0x10008`, `0x10010`, `0x10014` | clock / power words the BT bring-up touches (§5.1) | M:t |
| `0x11900-0x1197C` | Wi-Fi / RF analog block: PLL, filters, PA, DC / IQ trims; `0x11968` filter calibration, `0x11978` strobe / result | M:t |
| `0x13B00`, `0x13B04` | random-number source (the driver's `ble_hw_rand`) | M:t |
| `0x14000-0x14064` | wireless control: `0x14000` BT / RF enable, `0x14028-0x14034` RF-die SPI port, `0x14040-0x1405C` Wi-Fi clock / reset | M:t |
| `0x20000-0x201FF` | BR/EDR baseband and the 625 us BT clock | M:t |
| `0x28000-0x28054` | **BLE baseband registers** (§2.1) | M:t, M:s |
| `0x2FC00-0x2FCA0` | BT analog / configuration block (§2.2) | M:t |
| `0x2FC80`, `0x2FC84`, `0x2FCBC` | BLE enable, **baseband RAM base**, **end** | M:t |
| `0x2FD40-0x2FD60` | slot timer: 8 alarms in 625 us slots (§10) | M:t |
| `0x2FD80-0x2FD9C` | BT AGC configuration and the AGC table port | M:t |
| `0x30000-0x31FFF` | Wi-Fi MAC / BBP window; `0x3101C` the BBP byte port; `0x30F00/04` radio config | M:t |
| `0x41200-0x41218` | the chip's AES-128 block (the vendor's; Optimist's `BLE_AES_HW` hook is unused) | M:s |
| SRAM | the baseband RAM block (§4) is ordinary SRAM, anywhere the firmware chooses | M:d |
| Flash `0x0E8000` | **the SDK "VM" with the RF trims on an FM-1** (§14.1); the emulator's V15 keeps it at `0x093000` / `0x095000` | M:hw, M:d |
| Flash `0x0E9000` | BTIF: the classic BT address, VM record 102 | M:d |
| Flash `0x0FF000` | key_mac (factory MAC); all `FF` in the package | M:s |

BLE baseband registers are 32-bit words. The per-link registers are **not** MMIO: they are a 324-byte block in SRAM that
the engine reads and writes by DMA (§3).

## 2. Register reference

Confidence: **H** value and meaning agree across oracles; **M** value certain, meaning inferred; **L** only seen in the
binary.

### 2.1 BLE baseband (`0x28000-0x28054`)

| Addr | Acc | Bits and meaning | Observed | Conf |
|---|---|---|---|---|
| `0x28000` | RW | bit0 engine enable (first); bits 13, 14 set at init; bit4 cleared; bit6 soft reset [I]; bit2 hold in test mode [I] | `1`, `0x2001`, `0x6001` | M |
| `0x28008` | RW | [9:0] = 70, [15:12] = 0xF | `0xF046` | M |
| `0x2800C` | RW | [9:0] = 70: a timing offset in us (TX / RX path delay), subtracted from 625 [I] | `0x46` | M |
| `0x28010` | RW | [7:0] = 62, [15:8] = 62 | `0x3E3E` | L |
| `0x28014` | RW | [7:0] = 30, also subtracted from 625 [I] | `0x1E` | M |
| `0x28018` | RW | [7:0] = 20, [15:8] = 20 | `0x1414` | L |
| `0x2801C` | W | **link-column command**: `(column << 10) + (link << 4) + op`; op 5 = write the data word, op 2 = read the column into `0x28024` | | H |
| `0x28020` | W | column data word for op 5 (written before `0x2801C`) | | H |
| `0x28024` | R | column read result | | M |
| `0x28028` | RW | interrupt enables, link n: bit n **event** (IRQ 45), bit 8+n **RX** (IRQ 29) | `0x101` when advertising | H |
| `0x2802C` | W | acknowledge: write 1 to bit n / 8+n; also written with the enable bits before enabling | `0x001`, `0x101` | M |
| `0x28030` | R | pending flags; a source is served when (`0x28028` AND `0x28030`) has its bit | | M |
| `0x28034` | RW | bit0 = 1, [15:8] unknown | V15 `0x0A01`, demo `0x0901` | L |
| `0x28038` | RW | bit1 = engine busy (poll to 0 after stopping a link); bit7 a global flag acknowledged by writing bit6 | | M |
| `0x2804C` | RW | second interrupt group ("anchor-count timeout"), per link: bit n enable, bit 8+n acknowledge; served inside IRQ 45 | `0x100`, `0x101` | M |
| `0x28050` | R | second-group pending flags | | M |
| `0x2803C-0x28048`, `0x28054` | | low-power code only | not touched | L |

### 2.2 BT analog and configuration block (`0x2FC00-0x2FCBC`, `0x2FD80-0x2FD9C`)

`fm1_ble_rf_init` writes these in stock's order with stock's first-boot values (§5.4); the captured program supplies the rest.
`0x2FC88` holds the SRAM address of the PLL channel table (§2.4); `0x2FD98` / `0x2FD9C` are the AGC table's write index (= 0) and data
port (128 writes, identical in V15 and demo_ble); `0x2FC08` [9:0] / [19:10] and `0x2FC10` bytes 0-2 are trim fields filled from BBP
read-backs, not the VM (§16.2); `0x2FC98`, `0x2FC9C`, `0x2FCA0` are built to `0x02360236`, `0x36360008`, `0x00003636`; `0x2FC80` bit0 is
the BLE block enable (written just before `0x28000 = 1`); **`0x2FC84` / `0x2FCBC` are the baseband RAM base / end** (V15 3,682 B).
The rest (`0x2FC00-0x2FC28`, `0x2FC40`, `0x2FC44-0x2FC7C`, `0x2FC48`, `0x2FD80-0x2FD94`: AGC enable and thresholds [I]) are constants of
unknown meaning.

### 2.3 Link columns (through `0x2801C/20/24`)

Each link n (0 for the single link) has 17 16-bit columns. Meanings are inferred from the vendor's names for their
writers; the values are measured.

| Col | Meaning | Format / observed |
|---|---|---|
| 0 | anchor slot counter, low 16 bits | 625 us slots; written 0 at start. A read is a **countdown to the next anchor** [I]: it "stepped backwards" in a connection [M:hw], which is why the driver does not use it as a clock |
| 1 | event interval, low 16 bits | slots; advertising `advInterval` (V15 `0x00A0`), connection `connInterval x 2` |
| 2 | link state and latency | `(state << 12) + (latencyEnable << 11) + latency` (§2.5) |
| 3 | connection event counter | software reads it and subtracts 1; written 0 at set-up |
| 4 | transmit-window offset | `0x8000 + slots`; 0 = none |
| 5 | instant | event counter at which an update takes effect |
| 6 | channel selection | advertising `0x4000 + (hop << 8) + flags` (`0x41A5`: first 37, hop 1, all three); fixed channel `0x2000 + ch`; scan / initiate `0x2100 + ch`; connection `0x8000 + (hop << 8) + hop` |
| 7 | bit offset in the slot (us, 12-bit signed), bit15 widening-drift enable | written 0 at start |
| 8 | advertising random delay | `0xC000` on, 0 off |
| 9 | latency status; bit0 = 1 right after an initiating link starts ("init end") | |
| 10 | extended-mode bits; bit6 = channel selection #2 | not used |
| 14 | anchor slot counter, high bits | `0x8000 + (slots >> 16)` = **start**; 0 = **stop** (how a link is disabled) |
| 15 | event interval high bits and event enable | `(enable << 15) + (slots >> 16)`; bit15 is 1 for advertising and connections, 0 for scan / initiate |
| 11-13, 16 | extended advertising / PHY, widening-drift count | unused |

### 2.4 PLL channel table
81 entries of 3 words, one per 1 MHz channel, in SRAM at `0x2FC88`. V15 after boot: entry i = `{i + (i << 8), 0, 0}` (the driver
writes this; 972 B of RAM). The words' meaning is unknown.

### 2.5 Link states (column 2 [14:12])
1 scanning, 2 advertising, 3 initiating, 6 central (master) connection (also the direct-test transmitter), 7 peripheral
(slave) connection.

## 3. The per-link control block (324 bytes, in SRAM)

The engine reads and writes it by DMA. Names from the vendor's debug info. TX / RX pointer fields are **16-bit byte offsets
from the baseband RAM base**, below 65,535. `struct ble_cb` in the driver is `_Static_assert`ed against these offsets.

| Off | Field | Meaning | Stock V15 while advertising |
|---|---|---|---|
| 0x000 | ANCHOR | bit15 set at connection set-up; low bits = dead time 4 for 1M | `0000` |
| 0x004 | ADVIDX | [15:14] advertising PDUs per event (3 = 37, 38, 39); [13:0] spacing in us | `C4E2` (3, 1,250 us) |
| 0x006 | FORMAT | bit2 advertising; bit3 local address programmed; bit8 ignore SCAN_REQ (advertiser) / send none (scanner); bits 10-15 test payloads | `000C` |
| 0x008 | OPTCNTL | bit3 remote-address match disable, bit4 local-address match disable; at connection set-up bit2 set, bit9 cleared, bits 10-12 set | `0228` |
| 0x00A / 0x00C | BDADDR0 / 1 | access address [15:0] / [31:16] | `BED6` / `8E89` |
| 0x00E | TXTOG | bit0 engine-owned (software only reads it, §8.2); bit1 cleared at set-up; bits 1-3 engine state | `0000` |
| 0x010 | RXTOG | bit0 = which RX buffer is filled next; **while advertising the filled one is the buffer RXTOG has moved past** (§8.1) | `0000` |
| 0x012 / 0x014 | TXPTR0 / 1 | offsets of the TX buffer payloads | |
| 0x016 / 0x018 | TXAHDR0 / 1 | header byte 0: [3:0] type, bit4 TxAdd, bit5 RxAdd (Core bits 6 / 7 moved down) | `0000` ADV_IND, `0004` SCAN_RSP |
| 0x01A / 0x01C | TXDHDR0 / 1 | data header: [1:0] LLID, bit2 SN (per buffer, set once, kept), bit3 MD, [15:8] length | `2200`, `1000` |
| 0x01E / 0x020 | RXPTR0 / 1 | RX buffer payload offsets | |
| 0x022 / 0x024 | WINCNTL0 / 1 | receive window in us, [15:0] / [31:16] | `0032` |
| 0x026-0x02C | RXAHDR0 / 1, RXDHDR0 / 1 | received headers, Core layout (RXDHDR bit2 NESN, bit3 SN, bit4 MD) | |
| 0x02E-0x032 | CHMAP0-2 | channel map bits 0-36; CHMAP2 [15:5] = used count | `FFFF FFFF 04BF` |
| 0x034 | LASTCHMAP | channel index of the last received packet | |
| 0x036 | RXMAXBUF | maximum RX payload | `00FF` |
| 0x038 / 0x03A | RXSTAT0 / 1 | [3:0] = 1 a good packet (bits 2, 3 error kinds); bit8 the peer acknowledged our last TX. On an FM-1: [3:0] = 1 on every good packet, `0x9805` a dropped SCAN_REQ, bits 15 and 12 always set | |
| 0x03C | FILTERCNTL | bit0 whitelist entry valid, bit3 scan-request filter, bit4 connect-request filter, bit8 entry address type | `0000` |
| 0x03E | WHITELIST0 L/M/U | the one whitelist / target address (16-bit halves, little-endian) | |
| 0x044 / 0x046 | CRCWORD0 / 1 | CRC init [15:0] / [23:16] | `5555`, `0055` |
| 0x048 / 0x04A | WIDEN0 / 1 | window widening: whole slots / `0x5000 + (us mod 625)` | `0000`, `51E7` |
| 0x04C | TARGETADR L/M/U | peer address (directed modes, initiating) | |
| 0x052 | LOCALADR L/M/U | own address | |
| 0x058 | EVTCOUNT | event counter of the last RX (engine-written; zeroed at set-up) | |
| 0x05C | IFSCNT | inter-frame spacing control (constant) | `8295` |
| 0x05E / 0x060 | RFPRIOSTAT / CNTL | radio-arbitration priority (shared radio): 17, 19, 26, 28, 30 are used | `001A` |
| 0x062 | INTFRAME | bits 1, 2 set at init; bit3 TX disable; [5:4] = `01` in a connection; bit6 = MD of the PDU in the TX buffer | `0006` |
| 0x064 / 0x065 | TXBUF0 / 1CNTL | bit0 = **empty** flag (§8.2) | |
| 0x066 / 0x067 | RXBUF0 / 1CNTL | bit0 = 1: engine filled the buffer, 0: armed. **While advertising it stays 00**; in a connection and while scanning it marks the filled buffer [M:hw] | |
| 0x068-0x0E0 | FRQ_IDX0 / 1, FRQ_TBL0 / 1 | 40 B each: channel index tables and RF frequency as MHz - 2402 | |
| 0x112 | WINCNTL2 | normal receive window in us | `0032` |
| 0x120 / 0x128 | TX_PWER / TX_SET | TX power = level + 16 (stock level 6) | `0x16` |
| 0x130 | PLL_COMP | carrier-offset compensation from VM record 110 (V15 writes none) | 0 |
| 0x138 | RSSI0-3 | engine-written; RSSI dBm = 21 - (RSSI2 & 63) - T[(RSSI2 >> 8) & 63], T a 64-entry table (not transcribed) | |
| others | EXT*, RX_GAIN, MDM_SET, ANL_OUT | extended PHY / unused for 1M | 0 |

Decoded dumps: [ble-traces/v15-ble_param.txt](ble-traces/v15-ble_param.txt),
[ble-traces/demo_ble-ble_param.txt](ble-traces/demo_ble-ble_param.txt).

## 4. Baseband RAM layout

One contiguous SRAM block, zeroed, with `0x2FC84` = start and `0x2FCBC` = start + size. Everything the engine touches by
DMA lies inside it (16-bit offsets). Optimist's static 1,508-byte block:

| Offset | Size | Content |
|---|---|---|
| 0x000 | 16 | **instance table**: 8 x u16, entry n = `0x8000 + offset of link n's control block`; 0 unused. V15: `8040 0000 ...` |
| 0x010 | 48 | software area (unused) |
| 0x040 | 324 | link 0's control block |
| 0x184 | 2 x 284 | RX buffers: a 20-byte software header + 264 payload (advertising needs 263) |
| 0x3BC | 2 x 276 | TX buffers: 20-byte header + 256 payload (251 + MIC) |

TX / RX pointer fields point at the **payload**, 20 bytes into the buffer. Two RX and two TX buffers per link ping-pong. The
last two octets of each software header hold the PDU header, so the link layer reads and writes PDUs in place. Advertising
TX buffers hold AdvA (6) + data; at connection set-up they become the connection's two TX buffers. In V15 a link entity is
616 B, in demo_ble 636 B.

## 5. Init sequence (stock V15 first boot [M:t]; demo_ble the same)

**5.1 Clocks and power.** `0x10010` bit16 set then cleared, bit10 set; the Wi-Fi clock / reset block (`0x14040-0x1405C`);
before the BT block `0x10010` bits 14-15 cleared (BT domain), `0x14000` bit7, [20:16] = `0x0C`, bit0 (giving `0x000C0081`);
`0x2FC40` bit10, `0x20000` = `0xC0000000`, `0x2FC40` = `0xFFFF` then `0xFCFD`, `0x2FC78` = `0x0F` then `0x3FF`. The delay
lengths between the steps are unknown (`FM1_BLE_STEP_DELAY_US` = 100 us is a guess).

**5.2 RF analog: the Wi-Fi front end comes first.** The BT RF init calls the shared Wi-Fi RF init first; a BLE-only
firmware cannot skip it.

Its pieces: radio config + BBP / MAC init (`0x30F00/04`, `0x30308-0x31348`, ~3,300 BBP writes, constant); the Wi-Fi analog init
(`0x11900-0x11964`, ~250 writes, mostly constant); the **PLL VCO bank scan** (`0x11934` / `0x11938` / `0x1193C`, ~380 steps,
**live every boot**); the filter / DC / IQ / TX-LO calibration (~96,000 BBP writes, boot 1 only); the RF-die LUT (2 x 256 words,
constant base + patches, §16.1).

RF-die SPI protocol: wait until `0x14028` [22:20] read 0 (writing 0 to `0x14034` while waiting); data to `0x1402C`;
`0x14028` = `0x10000 + (address << 8) + command`; toggle `0x14028` bit4 five times high, five low (the "kick"). Commands:
`0xF` register write, `0x6` register read (result in `0x1402C`), `0xE` LUT word 0, `0xD` LUT word 1 (V15 uses only the last
two). BBP byte port `0x3101C`: bit17 start, bit16 read, [15:8] register, [7:0] data; every command also has bit 19 set
(constant, meaning unknown).

**5.3 Calibration and the stored-trim shortcut.** Both oracles store their calibration in the flash VM (§14) and reuse it:
V15 boot 1 takes about 117 ms emulated and writes the VM, boot 2 about 17 ms. On boot 2 the VCO scan and the LUT load still
run; the VM records drive part of its writes (§16.4), the rest is constant or live (§16.1, §16.2).

**5.4 BT analog block.** Order: the `0x2FC40` / `0x2FC78` writes of 5.1, `0x2FC88` (table in SRAM first), the AGC table
(`0x2FD98` = 0, 128 words), the AGC configuration (`0x2FC20`, `0x2FD80-0x2FD94`, `0x2FC7C`), `0x2FC48`, `0x2FC00-0x2FC28`,
the trim RMWs of `0x2FC10` / `0x2FC08`, `0x2FC98-0x2FCA0`. Stock then runs the BR/EDR baseband and slot timer (`0x2FD40` =
`0xFF00`); Optimist does not: an FM-1 connects without them.

**5.5 BLE baseband**, in this order: `0x2FC80` |= 1; `0x28000` = 1; `0x28034` = `0x0A01`; allocate and zero the RAM block,
`0x2FC84` = base, `0x2FCBC` = end; register IRQ 45 and 29 at priority 2; `0x28000` |= `0x2000`, |= `0x4000`, &= ~`0x10`
(giving `0x6001`); `0x28008` = (x & ~0x3FF) + 70 then |= `0xF000`; `0x2800C` = 70; `0x28010` = 62 + (62 << 8); `0x28014` = 30;
`0x28018` = 20 + (20 << 8). Opening a link: all 17 columns written 0, column 14 again, IRQ enables cleared. The control block
starts with AA `0x8E89BED6`, CRC init `0x555555`, ADVIDX `0xD388`, WIDEN1 `0x51E7`, OPTCNTL `0x28` then |`0x200`, TXDHDR0/1 =
1, RXMAXBUF 255, IFSCNT `0x8295`, RFPRIOCNTL `0x101`, RFPRIOSTAT 1.

## 6. Advertising set-up (V15 order [M:t, M:d])

1. Control block: RFPRIO 26 (19 when classic BT is in sniff); CRC init `0x555555`; WINCNTL0 = 50, WINCNTL1 = 0,
WINCNTL2 = 50. 2. Column 1 = interval in slots (`0xA0`), column 15 = `0x8000`. 3. FILTERCNTL from the filter policy
(1: bit3, 2: bit4, 3: both). 4. ADVIDX = `(packets << 14) + spacing` = `0xC4E2`. 5. Column 8 = `0xC000`. 6. Column 6 =
`0x41A5` (bit14 advertising mode; [12:8] hop 1; [5:0] first channel 37; bits 7 / 6 from the 37 / 38 / 39 mask). 7. Two RX
buffers armed (RXBUFnCNTL bit0 = 0). 8. TX buffer 0 = ADV_IND (AdvA + data, TXAHDR0 = type 0, TXDHDR0 = length << 8),
buffer 1 = SCAN_RSP (TXAHDR1 = 4). 9. OPTCNTL bit4 cleared (local-address match on); LOCALADR = own address; FORMAT =
`0x000C`. 10. Column 2 = `0x2000`. 11. IRQs: `0x2802C` = `0x001`, `0x101`; `0x2804C` = `0x100`; `0x28028` = `0x001`,
`0x101`; `0x2804C` = `0x101`. 12. Start: column 7 = 0, 0 = 0, 14 = 0, 0 = 0, 14 = `0x8000`.

The engine then advertises on its own and answers a SCAN_REQ within T_IFS from the SCAN_RSP buffer with no software
(FORMAT bit8 clear). Changing the data while advertising: the vendor copies it into the TX buffer inside the event ISR
and rewrites the header.

FRQ tables for an all-channels map: FRQ_IDX0[i] = i; FRQ_TBL0[i] = 2(i+1) for i <= 10, 2(i+1) + 2 for 11 <= i <= 36, and 0, 24, 78 for
channels 37, 38, 39 (MHz - 2402); FRQ_IDX1 / TBL1 hold the used channels packed in order.

## 7. Connection set-up from CONNECT_IND (peripheral)

It happens in the **RX interrupt** (IRQ 29) of the advertising link, before the ISR returns: link n turns from state 2
into state 7. The set-up must finish before the first anchor, transmitWindowDelay 1.25 ms + WinOffset after the end of the
CONNECT_IND (in the emulator the driver's state 7 goes in 8-16 us). The 22 LLData bytes are used as in the Core spec.
**Window widening** (us) = 2 + 2 x floor(Interval x 1,250 x (masterSCA_ppm + 200) x (latency + 1) / 10^6). Writes, in the
vendor's order:

1. RFPRIO 28; ANCHOR = `0x8000`; TXTOG bit1 cleared; RXTOG = 0. 2. BDADDR0/1 = AA; CRCWORD0/1 = CRCInit. 3. OPTCNTL |= 4,
&= ~`0x200`, |= `0xC00`, |= `0x1000`. 4. INTFRAME [5:4] = `01`. 5. TXBUF0CNTL and TXBUF1CNTL bit0 = 1 (both empty).
6. ANCHOR low bits = 4. 7. Column 5 = 0. 8. Column 4 = `0x8000 + (2 x WinOffset - 1)` if WinOffset > 0, else 0. 9. Column 2 =
`(7 << 12) + (latencyEnable << 11) + Latency`. 10. WIDEN0 = widening / 625; WIDEN1 = `0x5000 + (widening mod 625)`.
11. Column 8 = 0. 12. Column 3 = 0; EVTCOUNT = 0. 13. WINCNTL0/1 = WinSize x 1,250 + 1,250 us; WINCNTL2 = 50. 14. Column 1 =
2 x Interval, column 15 = `0x8000 + ((2 x Interval) >> 16)`. 15. The supervision timeout is a **software** timer. 16. Channel
tables and CHMAP from ChM (§6); column 6 = `0x8000 + (Hop << 8) + Hop`. 17. TXAHDR0/1 = 0; TXDHDR of the TXTOG-bit0 buffer =
`((TXTOG & 1) << 2) + 1`, the other that XOR 5 in the vendor (the driver writes LLID 1 in both with bit2 opposite, §8.2).
18. The connection's TX buffers are the two advertising TX buffers.

The anchor counter (columns 0 / 14) is not rewritten for a peripheral; the engine continues from the CONNECT_IND timing.
On the FM-1 the first data packet arrives 26-29 ms after the CONNECT_IND (interval 30 ms, WinOffset 22) [M:hw].

## 8. Per-event servicing (IRQ 45 event, IRQ 29 RX)

Both handlers loop over links and serve link n when its enable and pending bits are both set.

**IRQ 29 (RX)**: acknowledge (`0x2802C` bit 8+n). In a connection read RXTOG bit0 and both RXBUFnCNTL bit0 (repeat until
RXTOG reads the same twice); the RXTOG buffer is checked first, then the other, each only if its RXBUFnCNTL bit0 = 1; take
it, point RXPTRn at a fresh buffer, clear bit0 (re-arm). Decode from the control block: RXAHDR / RXDHDR, RXSTAT (status
[3:0] != 1 with bit2 or 3 = an errored packet), EVTCOUNT, LASTCHMAP, RSSI2 / 3. The TX service (§8.2) runs at the end of
every connection RX interrupt.

**IRQ 45 (event)**: acknowledge (`0x2802C` bit n). Advertising: refresh data, re-assert both TX buffers, RFPRIO 26.
Peripheral: read the event counter (column 3 - 1), clear a passed instant, per-event callbacks, latency, receive-window
auto-zoom. Second group: if (`0x2804C` & `0x28050`) bit n, set `0x2804C` bit 8+n and handle the link timeout. After the
loop, if `0x28038` bit7, write bit6. The event interrupt never runs the TX service.

### 8.1 Measured on the FM-1 (RX and clock) [M:hw]
- **Advertising RX**: RXBUFnCNTL stays `00`. A SCAN_REQ / CONNECT_IND is always in the buffer **RXTOG has moved past**
  (payload at RXPTRn, header in RXAHDRn / RXDHDRn, e.g. `2285` / `2200` for a CONNECT_IND). The driver tries that rule first.
- **Connection RX**: RXBUFnCNTL bit0 = 1 marks the filled buffer (470 of 470 packets, no desync).
- **Scanning RX**: RXBUFnCNTL bit0 marks the report (6,301 of 6,301; the RXTOG rule found none).
- **The slot clock** (columns 0 / 14, op 2) steps backwards in a connection: not a time base. The driver's clock is TIMER4
  (24 MHz) extended to 32-bit microseconds.

### 8.2 TX buffers: the vendor's contract (definitive)

TXBUFnCNTL bit0 is the **"empty"** flag: 1 = empty (software may fill it; a PDU software put there is finished), 0 =
software handed a PDU to the engine. Software loads a buffer and then **clears** bit0; it never sets bit0 after the
connection set-up. Read from the vendor library's IR and confirmed on the FM-1 once the driver followed it (the earlier
drivers had the polarity inverted, which explained every TX stall seen).

1. **TXTOG / RXTOG.** Software reads TXTOG bit0 only; it writes TXTOG twice (the whole register 0 in the frame init, bit1
   cleared at set-up) and never flips bit0. Bit0 names the buffer the engine is on; the service handles the TXTOG buffer
   first, then the other. Bits 1-3 (7, 5, 2, E ...) are engine state with no software meaning. RXTOG: software writes 0 at
   init and set-up, otherwise reads bit0.
2. **Software sets bit0 = 1** on both buffers at connection set-up and nowhere else; it **clears bit0** as the last step of
   loading a PDU; the engine sets it back to 1 when finished with the PDU.
3. **Loading.** The service runs at the end of every connection RX interrupt (and after queuing a PDU, in task context).
   One pass: read TXTOG bit0 and both empty bits twice (if TXTOG bit0 changed between the reads use the second reading);
   handle buffer T = TXTOG bit0, then 1 - T:
   - **empty = 1**: a PDU of ours recorded there is **acknowledged**: release it. Then take the next PDU (control first):
     payload to the buffer's fixed TXPTRn area (TXPTRn itself is not rewritten), TXAHDRn = 0, TXDHDRn =
     `(length << 8) + (MD << 3) + LLID` with **bit2 kept**, INTFRAME bit6 = MD, **then clear bit0** (the last write); record
     it there. Nothing queued: INTFRAME bit6 = 0 and bit0 stays 1; the engine sends empty PDUs.
   - **empty = 0**: the engine's. Untouched, except MD set on our PDU there when more was queued since.
   One pass can load both buffers; a PDU never moves between buffers.
4. **Acknowledgement** is bit0 back to 1 on the buffer you loaded; nothing else (no NESN comparison, no TXTOG, no EVTCOUNT).
   RXSTAT bit8 is used by the vendor only for the slave-latency decision.
5. **SN / NESN** belong to the engine. TXDHDR bit2 is written only at set-up (0 on the TXTOG buffer, 1 on the other) and
   preserved by every later header write; it never changed on the FM-1.
6. **Set-up, TX side**: both bit0 = 1; TXAHDR0/1 = 0; TXDHDR of the TXTOG buffer LLID 1 with bit2 = 0, the other LLID 1 with
   bit2 = 1; no PDU recorded in either; no TXPTR or TXTOG write. **Never** afterwards: bit0 = 1, a TXTOG write, a TXDHDR bit2
   change, a PDU moved between buffers, a buffer force-freed.
7. On the FM-1 [M:hw]: the engine cleared bit0 of the TXTOG buffer at the first event with nothing loaded; the rule
   tolerates it (a 0 buffer with no PDU of ours is skipped; counted as `tx_eng_held`). Acknowledgements arrive within 2
   events. Open: what TXTOG bits 1-3 count.

## 9. Update with an instant

On LL_CONNECTION_UPDATE_IND / LL_CHANNEL_MAP_IND software writes column 5 = instant and RFPRIO 30. In the event interrupt of
**instant - 1** it applies the new values: channel map to FRQ_IDX1 / TBL1 and CHMAP0-2 (§6); connection update to WINCNTL0/1
= WinSize x 1,250 + 625 us (+ widening), WINCNTL2 = 50, column 2 with the new latency, WIDEN0/1, column 4 = `0x8000 +
(2 x WinOffset - 1)` (or 0), columns 1 / 15 = the new interval. The window size is reset two events after the instant. An
update the driver sees late is applied at once and counted (`late_instant`).

## 10. Interrupts and priorities

| IRQ | Source | Priority (stock) |
|---|---|---|
| 45 | BLE event (and the second group `0x2804C/50`) | 2, CPU 0 |
| 29 | BLE RX | 2, CPU 0 |
| 40 | BT 625 us clock alarm (`0x200E4`; `0x2000C` bit9 enable, `0x20018` ack, `0x20010` pending) | 2 |
| 41 | slot timers (`0x2FD40`: bits 0-7 enable, write bit 8+k to ack, bits 16-23 pending; `0x2FD44 + 4k` alarm slot, 27-bit) | 2 |
| 11 | audio (not BLE) | 3 |

Optimist runs 45 and 29 at priority 2, below the audio (11) and the TIMER5 tick (63), never nested in each other. Stock's
BR/EDR stack keeps the BT clock and two slot timers busy every 0.5 s; a BLE-only firmware needs neither.

## 11. Stored trim and MAC records (the SDK VM; format in §14)

Ids (data bytes): 102 BT MAC (6, in BTIF `0x0E9000`, no area magic); 104 BLE MAC (6, not written by V15 / demo_ble); **106** XOSC
index (2: crystal load trims `xosc_l`, `xosc_r`); **107** Wi-Fi PA data (7: `PA_C_I`); **108** Wi-Fi PA MCS gain (20); 109 BLE local
info (34, host keys, random per first boot); 110 BT frequency offset (4 + CRC16, for PLL_COMP; V15 writes none); 113 BLE remote DB
info (14); **187** Wi-Fi RF init info (68: the calibration cache that shortens the next boot); 197 RF production test (28 +
CRC16). With no factory address in key_mac both oracles make a classic address at first boot and store it at `0x0E9000`; the BLE
address shares its upper 3 bytes. Optimist keeps its own random static address (BLE-STACK.md §6.2).

## 12. Open hardware questions

Settled on the FM-1 by the driver running (2026-10-08..10): the stored-trim start-up gives a working radio (advertising,
scan response, connections, MIDI both ways, the master role); the BR/EDR baseband and slot timer are not needed; the
CONNECT_IND set-up in the RX interrupt (§7) meets the first window; one unit's VM was read (§14.1).

| # | Open | Why it matters |
|---|---|---|
| U2 | the VM on a second unit; where stock's second area is | does calibration differ per unit? compaction? |
| U5 | timing constants `0x28008-0x28018`, `0x28034` [15:8], IFSCNT | measure T_IFS and PDU spacing with a sniffer |
| U8 | ISR durations on hardware (`isr_max_us` in `blell`) | audio headroom |
| U9 | RSSI gain table (64 entries) | the devices list's bars are relative until then |
| U10 | TX power level to dBm (stock level 6) | |
| U13 | drift: stock RMWs `0x11900` about every 2 ms (bit14, meaning unknown) | a periodic re-trim may be needed |
| U14 | carrier offset and PLL_COMP sign / scale | |
| U17 | do the BBP window read-backs `0xCB` / `0xD3` / `0xD7` return the addressed entry? | decides the 187 read-modify-writes and the left-out loop (§16.2) |
| C-list | §21.9 | |

## 13. Reproducing the measurements

The emulator is `fm1-emulator` (`~/GitHub/fm1-emulator-ble`, branch `feat/ble-engine`; it also models the BLE engine, scanning, states
3 / 6 and virtual centrals and peripherals). `examples/diagnose` takes `FM1_MMIO_TRACE=PATH`, `FM1_MMIO_RANGES=LO-HI,...`,
`FM1_MMIO_READS=1`, `FM1_FLASH_DUMP=PATH`, `FM1_FLASH_RESTORE=PATH` (a second boot over a saved flash). Raw traces kept in
`docs/ble-traces/`: `v15-boot-ble-mmio.txt`, `demo_ble-boot-ble-mmio.txt` (BLE / BT-config / BT-clock writes of the first 0.45 s), the two
`*-ble_param.txt` control-block dumps (§3), `v15-vm-trim-map.txt` (the VM extract `tests/ble_vm_test.c` and
`tests/ble_rf_capture_test.py` use).

## 14. The SDK VM store: record format and behaviour

Measured in the emulator with V15 and demo_ble on hand-built flash images, and on one real FM-1 for the location.

### 14.1 Where the VM is
**On a real FM-1 the VM is `0x0E8000` (4 KiB)** [M:hw, one unit, 2026-10-08, the console's `flr` on a unit booted on stock V15
and then updated to Optimist through stock's own updater]:
- `0x0E8000` starts `55 AA AA 55`, then records 106 (`0xE8004`, 2 B), 107 (`0xE800A`, 7), 187 (`0xE8015`, 68), 108
  (`0xE805D`, 20), 113 (`0xE8075`, 14), 109 (`0xE8087`, 34); the log ends at `0xE80AD`. 106 = `0B 0B`, 107 = `01 07 04 07 0B
  01 07`; 187's inner CRC holds.
- `0x0E9000` is BTIF, so a VM area at `0x0E8000` is at most 4 KiB. `0x0E7000` and `0x093000-0x096FFF` read `FF`.
- Unknown: where stock's second area is on hardware.

The emulator's V15: area A `0x093000`, area B `0x095000`, 8 KiB each (demo_ble: 64 KiB in two 32 KiB halves). The VM length is
the firmware's own setting.

### 14.2 Layout
Area header `55 AA AA 55` (erased: `FF`). Records follow from area + 4, packed (odd addresses). Each: byte 0 = **low byte of
CRC-16/XMODEM** (poly `0x1021`, init 0, no reflection, no final XOR) over the data only; byte 1 = id [7:0]; byte 2 = id [11:8]
in bits [3:0], length [3:0] in [7:4]; byte 3 = length [11:4]; then the data. The log ends at the first header whose check byte
does not match (an erased header always fails) or a record past the area. A write appends; nothing is rewritten in place.

### 14.3 Reading one id (what `ble_vm.c` does)
Candidate areas in order; the live one starts `55 AA AA 55` **and** has a valid first record. For each id keep the **last**
record whose check matches; a bad record ends the log (everything after it is invisible). Records with an inner check (187;
110 and 197 per the vendor library) are verified too. Never write the area.

### 14.4 Compaction
At boot the VM compacts when the log ends past 60 % of the area (demo_ble `0x4CCC` of 32 KiB; V15's threshold is probably
`0x1333` [I]): the other area is erased, the latest record of each id is copied in ascending id order, the magic is written
last, then the old area is erased. A 187 with a bad inner CRC makes V15 recalibrate in full and append a new 187.

### 14.5 The records an RF bring-up needs, and what each byte drives (V15 second boot, per-byte xor `0x03` runs)

| Id | Layout | Drives |
|---|---|---|
| 106 | byte 0 = `xosc_l`, byte 1 = `xosc_r` | `0x11930` [16:13] from byte 0, [22:19] from byte 1 (4 bits each) |
| 107 | seven `PA_C_I` values | byte 0 to `0x11924` [26:24]; 1 to `0x11928` [2:0]; 2 to [11:9]; 3 to [17:15]; 4 to [25:22]; 5 to `0x1192C` [2:0]; 6 no register. Every byte also patches 32 RF-die LUT words (entries `0xE0-0xFF` of both words) |
| 108 | 20 B | non-zero bytes replace window-D entries (§16.4); a zero keeps the default |
| 187 | 64 B payload + CRC-16/XMODEM of it, little-endian, at 64-65, + `00 00` | window-D / D' entries, `0x11908`, `0x1191C`, `0x11920`, `0x1195C`, `0x11960`, RF-die LUT words (§16.4). A stale inner CRC makes V15 discard the record and recalibrate |
| 109, 113 | random-looking / `06 1A 00 00` + 10 x `FF` | no RF write |
| 110 | | adding one changed nothing in the emulator (PLL_COMP stays 0) |
| 102 | BTIF `0x0E9000` | the BLE address: bytes 0-1 = CRC-16/XMODEM of the 6 classic bytes, 3-5 = classic 3-5 |

## 15. Optimist's flash use against the stock VM

The current map is `docs/MEMORY-MAP.md` and `firmware/hal/fm1_flash_map.h`. What matters here:

- **Nothing of Optimist writes `0x0E7000-0x0E9FFF`** (the retired UP_FM6 sector, the SDK VM, BTIF): `FL_NEVER` refuses
  `0xE7000-0xFBFFF` in the store, and the RAM flash driver refuses `0xE7000-0xE9FFF` itself. UP_FM6's voices live at
  `0x095000-0x096FFF` (`fix/upfm6-off-vm`); a unit with the old copies gets them copied over once. BLE writes only the
  settings record (`0x0FC000` / `0x0FD000`): the address, the radio's trim copy, the bond, the device store.
- **At install** (stock to Optimist): the update loader writes only `[0x4000, 0x93000)`; its record sweep erases a sector
  in `[0x93000, 0xFC000)` only when its last 256 bytes hold a valid update record, and never `0xE7000-0xE9FFF`. On the unit
  measured the VM survived stock's updater installing Optimist [M:hw]. Where stock stages its loader is not measured.
- **SLOOP 2.4 hazard**: 2.4's USR4 sample slot spans `0x0E7000-0x0FAFFF`, over the VM and BTIF. A 2.4 unit that loaded a
  USR4 sample over about 3.5 KiB (or erased USR4) lost the VM at `0x0E8000` (BTIF at about 7.5 KiB). Read the flash first:
  `tools/fm1_rescue.py FM-1.fwsc` without `--write` backs up all 1 MiB from UBOOT; the console's `flr 0xe8000 256` ...
  `flr 0xe8f00 256` reads the VM's 4 KiB.
- **Returning to stock and back**: a stock restore that erases the package's declared VM region (`0x093000`, 352 KiB) would
  make V15 recalibrate and write a new VM [I].
- **15.4 Capture-and-keep (built)**: at boot the VM is read in place through plain SPI reads; the latest valid 106 / 107 / 108
  / 187 (187's inner CRC checked) are kept in a 100-byte copy with the settings the first time a complete set is found.
  Precedence: the VM's complete set, the copy, none; with none the radio is never started (BLE-STACK.md §8.2).

## 16. RF start-up from the traces: constants, stored trims, live calibration

V15's second boot (stored trims present) is the path `rf_init` follows. It writes 34,274 words to the RF / BT ranges up to the
BLE baseband init (20,819 to the Wi-Fi MAC / BBP window, 7,023 to the WL analog block, 6,194 to the WL control block, 187 to
the BT block, 31 to BR/EDR, 13 to the BLE baseband). Writes were classed by perturbation: one VM byte xor `0x03` per run
(76 + 65 runs), and a perturbed model output (PLL comparator thresholds, filter result, BBP read-backs).

### 16.1 Boot-2 groups

| # | Group | Kind | What `rf_init` does |
|---|---|---|---|
| 1-3 | clocks; first analog word `0x11900`; Wi-Fi clock / reset and radio config (`0x14040-0x14050`, `0x1405C`, `0x30F00/04`) | constant (bit14 of `0x11900`: §16.2) | captured |
| 4 | **BBP + MAC first load**: ~3,300 BBP transactions + 24 MAC-window words | constant | captured |
| 5 | crystal trim `0x11930` | **VM 106** | §14.5 |
| 6 | Wi-Fi analog init `0x11900-0x11964` (20 registers) | constant | captured |
| 7 | **VCO bank scan** (`0x11934` / `0x11938` / `0x1193C`, `0x11968` start, `0x11978` strobed 8-9 times, comparator in `0x11978` bits 17 / 18) | **live, every boot** | our own bisection (BLE-STACK.md §8.1); also patches LUT word 1 |
| 8 | post-scan analog set-up | **VM 187** + constant | captured + the 187 fields |
| 9 | PA trims `0x11924` / `0x11928` / `0x1192C` | **VM 107** | §14.5 |
| 10 | **BBP second phase**: the four window tables again, the 187 window entries, a window-D read-modify-write block with 539 read-backs | tables constant; the RMW block live | reload captured; RMW block left out (3,537 transactions) |
| 11 | **RF-die LUT** (2 x 256 words) | constant base + patches: VM 107 to entries `0xE0-0xFF` of both words; VCO scan to word-1 entries `0x00-0x7F`; VM 187 bytes 48-63 to words in `0x80-0xDF` | captured base, patches as captured (the emulator's) |
| 12 | around the LUT load (`0x11950` x 32, `0x11924`, `0x1405C` ...) | constant but `0x11924` | captured |
| 13 | VM 108 to window-D entries | **VM 108** | §16.4 |
| 14 | BT block (§5.4) | constant except the BT TX trims (live) | §5.4 |

### 16.2 What stays live (needs the real radio)
- The VCO bank scan and the LUT word-1 entries it patches.
- The window-D read-modify-writes (group 10): a 128-step measurement loop whose results are read back and written (DC / IQ
  trimming [I]); entries `0x51`, `0x58`, `0x5C`, `0x5D`, `0x5E`, `0x61`, `0x62`.
- The BT TX trims: `0x2FC10` bytes 0 and 1 = a read-back >> 1; byte 2 = the negated read-back; `0x2FC08` [9:0] and [19:10] = the
  read-back. In V15's boot 2 they do **not** come from the VM. Optimist keeps stock's first-written values.
- Bit 14 of `0x11900` toggles with a periodic routine; not a trim.
- Filter calibration is live in boot 1 only; in boot 2 `0x1195C` / `0x11960` and the `0x11910` / `0x11920` fields come from VM 187
  bytes 40-47.

### 16.3 BBP indirect windows
17 BBP registers; four windows (the commit register is always written 1, last):

| Window | Commit | Entry address | Data | Read-back | Entries in the first load |
|---|---|---|---|---|---|
| C | `0xC8` | `0xC9` (0x00-0x7A) | `0xCA` | `0xCB` | 893 transactions |
| C' | `0xCC` | `0xCD` (0x00-0x47) | `0xCE` | none | 73 |
| D | `0xD0` | `0xD1` (0x00-0x5F) | `0xD2` | `0xD3` | 101 |
| D' | `0xD4` | `0xD5` (0x00-0x1C) | `0xD6` | `0xD7` | 27 |

Order: address, data, commit. Direct registers in the first load: `0x15`, `0x16`, `0x19`, `0x54`, `0x6A`. That a read-back returns
the addressed entry is [I] (U17).

### 16.4 The stored trims, byte by byte
**VM 187** (64-byte payload) drives: bytes 0, 4, 8, 12 window-D `0x61` 2-bit fields; 16, 20 `0x62` [1:0], [3:2]; 1, 5, 9, 13, 17,
21 window-D `0x04`..`0x09` bits [7:6]; 24 `0x1191C` [9:8]; 25 `0x11920` [1:0]; 26-29 window-D' `0x09`, `0x0B`, `0x0A`, `0x0C`;
32, 33, 36, 37 `0x11908` bits 7, 15, 17, 25; 40-43 `0x1195C` bytes 0-3 (byte 42 also `0x11910` [15:14], `0x11920` [11:10]); 44-47
`0x11960` bytes 0-3; 48-63 RF-die LUT words (5, 47, 6, 38 words per group of four). The other bytes (2, 3, 6, 7, 10, 11, 14,
15, 18, 19, 22, 23, 30, 31, 34, 35, 38, 39) drive nothing at boot. Field widths of the 1- and 2-bit fields are [I]. The capture
tool finds which write carries which field by perturbing each byte; it does not use this list.

**VM 108** (20 B): a non-zero byte replaces a window-D default.

| Byte | 0 | 1 | 2 | 3 | 5 | 7 | 9 | 11 | 12 | 14 | 16 | 19 |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| Window-D entry | `0x0B` | `0x0D` | `0x0F` | `0x11` | `0x1B` | `0x1D` | `0x1F` | `0x21` | `0x13` | `0x15` | `0x17` | `0x19` |
| Default | `0x20` | `0x20` | `0x20` | `0x20` | `0x20` | `0x2B` | `0x26` | `0x20` | `0x20` | `0x2B` | `0x26` | `0x20` |

Bytes 4, 6, 8, 10, 13, 15, 17, 18 change nothing at boot.

## 17. The build-time capture

The long constant tables (about 3,300 + 3,411 BBP transactions, 240 analog words, 128 AGC words, 2 x 256 LUT words) are the
stock firmware's own writes. `tools/ble_rf_capture.py` makes them: it runs V15 (the user's `FM-1.fwsc`, refused unless its
SHA-256 is `db1642b2…`) in the emulator three times (first boot; second boot, traced; second boot with every trim byte xor
`0x03`), cuts the boot-2 trace into the §16.1 groups **by register patterns only**, maps each write the perturbed boot changes
to its field, and pins the result's SHA-256 (`30ba97ea…`). The output is committed as `firmware/hal/ble_rf_tables_v15.h`
(format 3, the program packed 10,706 to 5,795 B, about 40 KB of source), so a BLE build needs no V15 file; the tool is the
provenance, and `tests/ble_rf_capture_test.py` compares a fresh capture with the committed file when V15 and the emulator are
there. Format, section markers and build integration: BLE-STACK.md §8.1, §8.3. The repository carries these vendor-derived
values by the owner's decision (`LICENSING.md`).

## 18. Hardware bring-up: what is done

Done on an FM-1: a whole-flash backup and the VM read (`tools/fm1_rescue.py` without `--write`, `flr`); the radio start-up
from the VM; advertising seen and connected by a Mac; data and MIDI both ways; the master role (§21). Not done: a sniffer
measurement (T_IFS, spacing, TX power), a carrier / PER measurement, the read-back peeks of §16.2 (the console has no RF
register peek; `memr` reads RAM and XIP only), a cold-to-warm soak, a unit with no VM (the "none" path is host- and
emulator-tested only).

## 21. Central role (scan / initiate / master)

No new registers: a central uses the same control block, column port and interrupts; states 1 (scan), 3 (initiate), 6
(master). Source: the vendor library's IR; the C-questions of §21.9 were answered on the FM-1.

**21.1 Summary.** The engine answers within T_IFS by itself: a **SCAN_REQ** after an ADV_IND / ADV_SCAN_IND (FORMAT bit8
clear) and the pre-loaded **CONNECT_IND** after an ADV_IND / ADV_DIRECT_IND from the programmed target [M:hw]. Software builds
the CONNECT_IND **before** initiating starts, rotates the scan channel, runs the active-scan backoff, filters beyond one
address, resolves RPAs, runs the supervision timer and every LL procedure. The hardware "whitelist" is **one address**
(WHITELIST0 + FILTERCNTL); there is no IRK or resolving list in the engine. Master differs from slave only in the set-up
(anchor counter written, no widening), the steady receive window and the update's column 4 formula; the RX rule (§8) and TX
rule (§8.2) are the same code for states 6 and 7.

**21.2 Scanning (state 1)**, in the vendor's order: RFPRIO 17; column 8 = 0; window and interval in slots (window + 4 <= interval);
WINCNTL0/1 = window x 625 us, WINCNTL2 = 50; column 1 = interval, column 15 = interval >> 16 with **bit15 = 0**; FILTERCNTL bit3 for
filter policy 1 / 3; FORMAT bit8 cleared for an active scan; column 6 = `0x2100 + 37`; two RX buffers (263 B); two TX buffers hold a
**SCAN_REQ** (type 3, TxAdd = own type, length 12, ScanA, six zero bytes where AdvA goes: the engine fills AdvA [M:hw]); column 2 =
`0x1000`; OPTCNTL bit9 cleared, bits 10-11 set; interrupts as §6 step 11; the anchor counter started at once. Each event (IRQ 45):
column 6 = `0x2100 + ch` with the software's current channel, then 37, 38, 39, 37 (applies to the next window [I]); RFPRIO 17, and 26
every 6th event. RX (IRQ 29): the connection path of §8, the report marked by RXBUFnCNTL bit0 [M:hw]; software drops types 3 and 5.
A report: RXAHDR [3:0] type, bit5 ChSel, bit6 TxAdd, bit7 RxAdd; RXDHDR [15:8] length; payload AdvA + data; RXSTAT [3:0] = 1 good.
The active-scan backoff (Core Vol 6 Part B 4.4.3.2) is software: after each ADV_IND / ADV_SCAN_IND FORMAT bit8 = 0 only when
backoffCount = 1. Stop: column 14 = 0, optionally poll `0x28038` bit1, IRQ bits cleared, all 17 columns 0, instance-table entry 0.

**21.3 Initiating (state 3).** The CONNECT_IND is complete before the link starts: random AA, **the vendor's CRCInit is the fixed
`0x1983AE`** (Optimist uses a random one), WinSize 2, WinOffset random in [Interval / 2, Interval - 1], Hop random 5..16, SCA 0.
Engine programming: RFPRIO 26; window, interval, column 6 = `0x2100 + 37`, column 8 = 0 as scanning; LOCALADR = own address, FORMAT
bit3, OPTCNTL bit4 cleared; target: WHITELIST0 = TARGETADR = peer, FILTERCNTL = (FILTERCNTL & `0x7E`) + 1 + (type << 8) then bit4,
OPTCNTL bit3 cleared; **both** TX buffers hold the same CONNECT_IND (type 5, TxAdd = own type, RxAdd = peer type, length 34: InitA,
AdvA, 22 LLData bytes; TXAHDRn = type + (TxAdd << 4) + (RxAdd << 5), TXDHDRn = 34 << 8); column 2 = `0x3000`; interrupts and the
anchor counter start, then **column 9 = 1**. The vendor takes reception the advertising way; on the FM-1 the reports are marked
by RXBUFnCNTL, so Optimist takes each buffer by RXBUFnCNTL bit0 or by RXTOG having moved past a written header (both counted).
A hit: an ADV_IND / SCAN_RSP / ADV_SCAN_IND with AdvA and TxAdd equal to the target, or an ADV_DIRECT_IND to us.

**Switch to master (state 6)** in the **event** interrupt after the hit (not the RX ISR): the part shared with §7 (RFPRIO 28,
ANCHOR, TXTOG bit1, RXTOG, AA, CRC, OPTCNTL, INTFRAME, both TXBUFnCNTL bit0 = 1, column 5 = 0); then, master only, column 4 = 0,
**anchor counter = 2 x WinOffset + 4 slots** (column 7 = 0, 0 = 0, 14 = 0, then column 0 = 2 x WinOffset + 3 and column 14 =
`0x8000`, twice), column 2 = `0x6000 + Latency`, no WIDEN write; the shared tail (columns 8, 3, EVTCOUNT, WINCNTL0/1 = WinSize x
1,250 + 1,250 us, WINCNTL2 = 50, columns 1 / 15, channel tables, column 6, TX headers as §8.2 point 6); at the first packet received
WINCNTL0/1 = 0, WINCNTL2 = 30 us. On the FM-1 [M:hw]: the engine sends our CONNECT_IND by itself; the event interrupt comes 510 us
(max 1,883) after it; the peer answers the first anchor and every event after. The first anchor can fall outside the transmit
window when the event interrupt is late (set-ups 1.9 and 4.8 ms after the hit gave 0x3E): the driver shortens the anchor counter by
the slots elapsed (`hw_first_anchor`).

**21.4 Master (6) against slave (7).**

| | Slave (7) | Master (6) |
|---|---|---|
| first to transmit | the central | the engine, at the anchor |
| column 2 | `0x7000 + (latencyEnable << 11) + Latency` | `0x6000 + Latency` |
| anchor counter at set-up | not written | 2 x WinOffset + 4 |
| column 4 at set-up | `0x8000 + (2 x WinOffset - 1)` or 0 | 0 |
| widening | WIDEN0/1 and auto-zoom | none |
| receive window after the first packet | 2 x widening + 50 / 50 | WINCNTL0/1 = 0, WINCNTL2 = 30 |
| RX / TX service | §8 / §8.2 | the same code |
| update applied at instant - 1 | column 4 = `0x8000 + (2 x WinOffset - 1)`; WINCNTL0/1 = WinSize x 1,250 + 625 + widening; WIDEN | column 4 = `0x8000 + 2 x WinOffset`; WINCNTL0/1 = WinSize x 1,250 + 625; column 2 = `0x6000 + Latency` |

The instant counts as reached when (instant - counter - 1) as a 16-bit value exceeds 32,765. **Updates we start as master**:
LL_CONNECTION_UPDATE_IND instant = counter + 8..11 (+ 6 x Latency + 6 with latency), WinSize 1, WinOffset = Interval / 2,
applied when the PDU is acknowledged (column 5 = instant, RFPRIO 30), the 0 / 30 window two events after;
LL_CHANNEL_MAP_IND instant = counter + 6 + random 0-3. The master's TX payload areas are the 263 B CONNECT_IND buffers.

**21.5-21.8.** Each role runs on its own instance (the first free table entry of 8; each needs its own control block and buffers). The
vendor never overlaps two instances in the radio but time-slices in software; Optimist runs **one link at a time**, time-sliced with
advertising. RFPRIO: scanning 17 / 26, initiating 26 / 30, master set-up 28, in a connection 30 around instants and with control PDUs
queued, 28 with data queued. The vendor's CONNECT_IND carries SCA 0; no timing register is written on any central path.
ADV_DIRECT_IND (not used): type 1, length 12, AdvA + TargetA in both TX buffers. Address resolution is software only (no register
holds a key): Optimist resolves each RPA AdvA with `ah(IRK, prand)` and initiates to the literal RPA just heard.

**21.9 Hardware answers.**

| # | Question | Answer |
|---|---|---|
| C1 | the engine fills AdvA into the SCAN_REQ | **yes** (scan responses came back) |
| C2 | RXBUFnCNTL bit0 marks a report while scanning | **yes** |
| C3 | the engine sends the CONNECT_IND by itself | **yes** |
| C4 | event IRQ after the CONNECT_IND | 510 us, max 1,883 |
| C5 / C6 | first anchor inside the window; master transmits first, the 30 us window catches the reply | **yes**, with the late-IRQ correction |
| C8 | column 15 bit15 = 0 while scanning | scanning repeats; the bit's meaning is unknown |
| C7, C9-C12 | column 0 / 14 a countdown; FILTERCNTL bit3 while scanning; two overlapping instances; INTFRAME bit3 suppressing automatic responses; the scan channel written in the event ISR applying to the next window | open |
