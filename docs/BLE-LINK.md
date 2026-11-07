# Bluetooth MIDI and the two-FM-1 USB link: a plan (2026-11-06)

The owner: "I'd like to see about adding those features to ChoralRoot", about two things Melodee has now: **Bluetooth
MIDI** and **two FM-1s passing MIDI over one USB-C cable**. This is the plan, written before anything is built:
what Melodee did, what it would cost here (measured where it could be), the risks, the order of work and the
decisions that are the owner's. Nothing in the firmware changes with this document (HISTORY 3y226).

**The short version**

- Melodee's Bluetooth is not JieLi's: it is **Optimist's bare-metal BLE stack** (w0ts/optimist), about 7,500 lines
  of portable C with its own driver for the WL82 radio. No JieLi Bluetooth library is linked.
- Melodee's "link" is not a link protocol: it is a general **USB HOST** mode. One FM-1 is switched to host in the
  menu; the other stays an ordinary USB-MIDI device. The FM-1 cannot power the cable, so Melodee's own notes say to
  use a powered hub or a powered OTG adapter.
- **Bluetooth does not fit ChoralRoot as it is.** Melodee's whole Bluetooth feature set, compiled with ChoralRoot's
  compiler, is **49.9 KB** of flash; ChoralRoot has **15.8 KB** free in its 573,440 B slot. The peripheral role alone
  (a Mac, iPad or phone connects to the FM-1) is about 43 KB.
- **It fits in one package** if about 42 KB of read-only data (three of the four UI fonts and the radio's start-up
  program) moves out of the app slot into a 48 KB **resource region** at flash 0x93000..0x9EFFF, as Melodee did in
  1.6. That region is 16 KB nobody uses today plus 32 KB of store sectors that have been read-only leftovers since
  0.22 (the old VA, FM6 and CZ-1 stores, kept only for the migration).
- The USB host mode is small (Melodee's PR said +3.2 KB; about 5..8 KB now) and fits today.
- Recommended order: the resource region first (nothing new for players, released on its own), then Bluetooth MIDI
  with the FM-1 as the peripheral, then USB host, then connecting out to Bluetooth keyboards (which also gives a
  wireless link between two ChoralRoots). The SMK-37 later. Decisions in section 8.

## 1. What Melodee did

Melodee: github.com/keremimo/melodee, GPL-3.0-only, by Kerem Kilic (Ellic Studio). Read at `main` 165d177
(2026-11-02) and `nightly` 3fe570d (2026-11-03); both carry everything below except the two commits marked nightly.
Releases 1.6.0 and 1.6.1 ship both features.

### 1.1 The commits

| commit | date | author | what |
| --- | --- | --- | --- |
| fef995c | 2026-10-26 | Kerem Kilic | **feat(ble): add MIDI and calibration recovery**: the Optimist import (pinned at w0ts/optimist 498a3ae4), the glue `midi_ble.c`, the UI, settings, 22 test files; 107 files |
| 89b746c (PR #90, merged as affb83c 2026-10-27) | 2026-10-26 | Charles Vestal | the radio measures its own calibration at start when none is stored (`hal/fm1_rf_cal.h`, JieLi's SDK calibration rewritten in C) |
| 5320bc0, f2687d6, 9d33b69 | 2026-10-27 | Kerem Kilic | calibration kept in POOL, the helper removed, the Bluetooth screens built for size |
| 438ef0c | 2026-10-27 | Kerem Kilic | **feat(storage): move read-only assets into a resource region**: the radio program, a font and the factory sounds leave the app slot (flash 0xCB000..0xDFFFF on the FM-1) |
| 0170859 | 2026-11-01 | Kerem Kilic | VISIBLE mode for computers; the radio state and the last device kept across power-on |
| 38c8b2f, 4c40bb5 | 2026-11-02 | Kerem Kilic | clock, Start and Stop sent over USB and BLE; clock and transport in their own queue, ahead of notes |
| ca6c583 (nightly) | 2026-11-03 | Kerem Kilic | a peer paired as central can connect back to VISIBLE |
| 0d705bf, 198c8fd (PR #75) | 2026-10-21 | recursivefieldinstruments ("recursive") | **feat(usb): a USB MIDI host mode for the USB-C port**, input; "+3.2 KB flash, +400 B RAM" |
| b4380f9, eb57309, 44a311a | 2026-10-24/25 | Kerem Kilic | MIDI out in host mode, disconnect debounce, no idle probe loops |
| 3b64dcf, 7eb442e, 6e61576 | 2026-10-28 | Kerem Kilic | audio+MIDI devices, a hub on a hub, longer resets |
| 4f46baf (nightly, PR #115) | 2026-11-03 | recursivefieldinstruments | more hubs, says why no MIDI device was found |

Also seen: PR #112 by Jacek Mariański (jmarianski), "Bluetooth LE MIDI: native, on Optimist's open BLE stack",
closed unmerged; it measured its own port at **+40 KB image, +3.3 KB RAM, ~6 KB claimed from the audio arena**,
which agrees with the measurement below. Issue #120 (open, 2026-11-05): on one user's 1.6.1 unit VISIBLE never
appears to an iPhone or a Mac and CONNECT OUT never finds anything. Issue #89 (closed 2026-10-30): host mode with a
second FM-1 as the client failed in an rc and works from 1.6.0-rc.6, "can send midi from host to client fm-1 and
also from client to host"; the reporter does not say which cable worked.

### 1.2 Bluetooth MIDI

- **Which stack.** Optimist's own (w0ts/optimist, "the Optimist contributors"; the pinned commit is by hdavid). It
  was written clean-room from the Bluetooth Core spec (LE 1M only) and the MIDI Association's BLE-MIDI spec. Its
  driver comes from a fact sheet of the WL82 baseband registers (`docs/BLE-HW-FACTS.md`, read from the vendor
  binary by a separate reader). **No JieLi BT controller (BTCTRLER) or btstack library is linked.** This is the
  opposite of FIZZ and RemoteFM1, which use fm1-base's JieLi libraries (about +124 KB flash and +45 KB RAM on
  fm1-base's template; +82 KB of app slot in FIZZ).
- **What it links.** `firmware/src/ble/` (link layer peripheral + central, host, ATT server and client, SMP
  responder and initiator, AES-CCM, BLE-MIDI packets, the scan table, one remembered device), the WL82 driver
  (`ble_hw_wl82.c`, `hal/fm1_ble.h`), the radio start-up (`hal/fm1_ble_rf.h`: a 5,795 B packed program of 522
  register writes, 1,179 baseband window writes, 512 LUT words and a VCO scan, captured from stock V15),
  the trims reader (`ble_vm.c`) and the self-calibration (`hal/fm1_rf_cal.h`).
- **Pairing and advertising.** One connection, either role. **VISIBLE**: advertises `Melodee XXXX` every 100 ms on
  37/38/39 with the BLE-MIDI service UUID; a Mac (Audio MIDI Setup) or a phone app connects. **CONNECT OUT**: scans
  (40 ms interval) while the Bluetooth page is open, lists BLE-MIDI devices, connects; reconnects the last device
  (10 s of scanning every 30 s). LE **legacy** pairing only: Just Works for peers without a keyboard (another FM-1,
  most controllers), a six-digit passkey shown on the FM-1 when the peer can type. No Secure Connections. The bond
  (28 B) and the address live in Melodee's settings record, never in the stock VM.
- **MIDI over BLE.** GATT service `03B80E5A-EDE8-4B33-A751-6CE34EC4C700`, characteristic
  `7772E5DB-3868-4112-A1A9-F2669D106BF3` (read, write without response, notify). Real 13-bit millisecond
  timestamps both ways, running status, SysEx across packets. It asks for a 7.5..11.25 ms connection interval
  (then 15 ms after a reject); as central it uses 11.25 ms. Melodee keeps the 27-octet link-layer packets that stock
  uses ("data-length extension remains disabled pending measurements"), though the stack can do 251. Latency is
  therefore about one connection interval (7.5..15 ms) plus the jitter the timestamps take back out; Melodee
  publishes no latency measurement. Incoming notes are queued in the BLE interrupt, moved by TIMER5 into the same
  input ring as USB and TRS, each timed by its timestamp. Outgoing: whatever the FM-1 sends over USB also goes to
  BLE, and the clock with Start/Stop when the clock is internal. **SysEx, editor commands and updates are not
  routed over BLE.** No USB-BLE bridging.
- **Audio CPU.** The two BLE interrupts (event and RX) run at priority 2 on core 0, below the audio and TIMER5,
  never nested in each other. The baseband does every radio deadline in hardware; software only has to refill
  within an interval. The handlers run from flash; Optimist measured at most 36 us per handler at 96 MHz in its
  emulator; **on the FM-1 unmeasured**. With encryption, AES runs in the RX interrupt in software (7 AES blocks per
  MIDI notification). **Starting the radio holds interrupts** (the RF program, plus about 160 ms of calibration
  when no trims are stored): Melodee refuses to start it unless music is stopped, voices are released and USB
  audio is idle ("STOP AUDIO TO START BLE"), and at boot it starts before the audio does, as stock V15. Core 1 stays
  in its RAM idle loop during the start-up. A crystal ramp inside the start-up must run from RAM: the FM-1 hung in
  it when the table straddled a flash cache line.
- **USB.** Untouched: USB MIDI, USB audio and TRS keep working beside BLE.
- **Power.** Off by default; the radio state is saved; a radio left on starts again at the first quiet moment after
  boot. No measured current draw is published.
- **The trims.** The radio wants the factory calibration (VM records 106, 107, 108, 187 at flash 0xE8000 on an
  FM-1). Melodee reads them read-only and keeps a CRC-checked 100 B copy in its settings; with none it measures
  187 itself and uses the SDK board constants for the rest (checked against the SDK's own measurement on one unit).

### 1.3 The "link": USB HOST

- One FM-1 is set to **USB HOST** in the menu; it is **never saved** (every power-on is a normal USB device, so the
  update paths always work). The other FM-1 stays a USB-MIDI device and needs no setting.
- The roles are the player's choice. Both hosts: nothing enumerates (`NOTHING ON THE USB PORT`). Neither: two
  devices, nothing happens. While the port is a host the computer link (USB MIDI, USB audio, updates) is gone.
- **Cable and power.** The firmware never drives VBUS; the FM-1's USB-C cannot source 5 V. Melodee: "a direct USB-C
  link to a self-powered device does not connect. Use a powered hub or a powered OTG adapter"; "an USB-C to USB-A
  OTG adapter or a hub with a USB-A downstream port is the safer first test". So: host FM-1 -> powered OTG adapter
  or powered hub -> the second FM-1's USB-C. Whether a plain C-to-C cable works between two battery FM-1s is not
  established (issue #89's reporter tried one; it is not said what finally worked).
- **What passes.** Host in: channel messages and clock / Start / Continue / Stop; **SysEx from the device is
  ignored by design**. Host out: Melodee's MIDI tracks and the clock and transport from their queue, to cable 0 of
  the device's OUT endpoint. Full-speed devices only, one hub level (two hub chips in one case counted), no editor
  traffic.
- The register sequences follow the AC79 SDK's USB0 host code (`usb_h_sie_init`, `usb_h_chirp_and_reset`,
  `usb_ctlXfer`); 772 + 138 lines, tested against a model of the SIE, a keyboard and a hub.

### 1.4 Licence and credits

All GPL-3.0 compatible with ChoralRoot (GPL-3.0-only):

- Optimist's stack and driver: GPL-3.0-only ("the Optimist contributors"; upstream now offers the BLE stack as
  GPL-3.0-only OR MPL-2.0). Melodee's integration: Copyright (C) 2026 Kerem Kilic (Ellic Studio), GPL-3.0-only.
- `hal/fm1_rf_cal.h`: Charles Vestal's C rewrite of JieLi's `wl_rf_common.a` calibration (felucca-bsp), Apache-2.0
  (one-way compatible with GPL-3.0; its notice and `LICENSES/Apache-2.0.txt` come with it).
- USB host: recursivefieldinstruments and Kerem Kilic, GPL-3.0-only. Saadet (Kerem Kilic) informed the BLE client
  behaviour.
- **One provenance flag**: `ble_rf_tables.h`, the radio's start-up program, is register data captured from the stock
  M-VAVE V15 firmware. Optimist and Melodee both say it is "not independently authored" and has no stated licence;
  the code's GPL tag does not cover it. Melodee ships it in its resource region (decision 5).

## 2. What it costs in ChoralRoot

### 2.1 Where ChoralRoot stands (main d7b509f3, `./build.sh`, 2026-11-06)

| | used | of | free | the build's own rule |
| --- | --- | --- | --- | --- |
| XIP (the app slot) | 557,640 | 573,440 | **15,800** | must fit |
| RAM (.data + .bss) | 80,592 | 98,304 | 17,712 | (stack audit) |
| POOL | 331,492 | 344,064 | 12,572 | keep 8,192 spare: **4,380** usable |
| RAMTEXT | 21,660 | 24,576 | 2,916 | keep 512 spare: 2,404 usable |
| SMK-37 build XIP | 558,864 | 573,440 | 14,576 | |

### 2.2 Measured: Melodee's Bluetooth, compiled here

A throwaway probe (not committed): Melodee nightly 3fe570d's `midi_ble.c` with the whole stack, the WL82 driver,
the radio start-up and the self-calibration, every glue entry point kept, compiled by ChoralRoot's toolchain
(`clang -target pi32v2 -Os -ffunction-sections`). The stack is a unity build of `static` functions, so what the
glue never reaches is dropped, as it would be in the firmware.

| part | XIP | RAM | POOL | RAMTEXT |
| --- | --- | --- | --- | --- |
| code (`.text`) | 41,920 (about 190 of it is Melodee's fault handler, which ChoralRoot has its own of) | | | |
| constants | 2,193 | | | |
| the radio's start-up program | 5,795 | | | |
| state | 4 (.data) | 3,464 (link layer 1,416, scan table 680, ...) | 4,792 (baseband block 1,508, glue state 1,888, PLL table 972, saved trailer 212, ...) | 36 (the crystal ramp) |
| **Melodee's whole set** | **49,908** | **3,464** | **4,792** | **36** |
| the same at `-Oz` | code 37,778 (-4,142) | | | |

Split by part (attributed by function; the inlined glue is counted whole):

| feature set | XIP | how |
| --- | --- | --- |
| everything Melodee has (peripheral + central + legacy pairing + bond + self-calibration) | 49.9 KB | measured |
| without the central role (no connecting out: scanner, initiator, GATT client, passkey initiator, device list) | about 42.4 KB | measured total less the central's functions (~7.5 KB) |
| peripheral without pairing (a Mac connects; no bond, so no remembered link) | about 41 KB | less AES and SMP (~1.4 KB) |
| ChoralRoot's own screens and Options for it | +1..2 KB | estimate |
| `-Oz` on the main-loop half of it (the interrupts stay `-Os`) | -2..-3 KB | estimate from the `-Oz` probe |
| USB host (Melodee's `usb_host.c` today) | +5..8 KB XIP, +0.4..0.6 KB RAM | estimate: PR #75's input-only version was +3.2 KB / +400 B; output, hubs and messages came since |

### 2.3 Does it fit?

**No, not as ChoralRoot is now.** XIP: Bluetooth needs 41..50 KB, there is 15.8 KB. POOL: +4.8 KB would leave 7.8 KB,
under the build's 8 KiB rule; the glue's 1.9 KB state can live in RAM instead (RAM then +5.4 KB: 85,944 of 98,304 B,
87.4 %), leaving POOL 9.7 KB spare. RAMTEXT: +36 B, fine. USB host alone fits today.

### 2.4 The ways to make room

| option | XIP freed | what it costs |
| --- | --- | --- |
| **A. A resource region** (Melodee 1.6's way): read-only data moves to flash 0x93000..0x9EFFF, read through the plain XIP window (XIP 0x0208F000..0x0209AFFF). The fonts M (11,574), CRB (10,874) and CRX (13,576) and the radio program (5,795): 41,819 of 49,152 B. The 16 px icons (6,673) also fit: 48,492 | **36,024** (fonts) or **42,697** (+ the 16 px icons); the radio program never enters XIP | the region's 32 KB at 0x97000..0x9EFFF are the legacy VA store, FM6 stores and CZ-1 tone store 1..16, **read once since 0.22** for the migration and never written: retiring them ends the migration from 0.21 and older (decision 2). The loader learns one more record type; the app checks the region's header and CRC against its own build. No pixel and no audio change (the same flash, read the same way) |
| A2. More legacy sectors, as second windows | up to 32 KB more | CZ-1 tones 17..32 (0xA0000, 8 KB), the QUAD store (0xB2000, 8 KB), the 0.21 user banks (0xDC000, 16 KB): all legacy since 0.22 |
| B. Retire the migration code too | 2..5 KB (unmeasured) | the readers of the legacy stores; same decision as A |
| C. **A separate Bluetooth package** (FIZZ's route) | as needed | two packages for every release (x2 again for 1-core and SMK), the installer asks which, switching erases what does not fit. FIZZ gave up half its projects and presets; ChoralRoot would give up e.g. 4 of its 10 loop slots (32 KB of store as a code blob) or an engine (below) |
| D. Drop something, for everyone or in a Bluetooth build | GRAIN 12.6 KB, P5 29.7 KB, VA 16.7 KB, CZ-1 42.3 KB, FM TONE 49.0 KB (their symbols in XIP) | sounds players have |

**With A, Bluetooth fits in one package**:

| build | XIP used | free |
| --- | --- | --- |
| main today | 557,640 | 15,800 |
| A (fonts and 16 px icons out) | 514,943 | 58,497 |
| A + Bluetooth peripheral with bond + its screens (~36.7 + 1.5 KB) | ~553,100 | ~20,300 (96.5 %) |
| ... + USB host (~7 KB) | ~560,300 | ~13,100 (97.7 %) |
| ... + the central role (~7.5 KB) | ~567,500 | ~5,900 (99.0 %): tight; B and `-Oz` give back 4..8 KB |

Other room the plan does not need: 0xE5000..0xE6FFF (8 KB) is unused by ChoralRoot (fm1-base and FIZZ put their BLE
store there; avoid 0xE7000, which Melodee's reader treats as a possible VM).

### 2.5 The CPU on the heavy cases

The worst halves on the unit today (of the 2,902 us half), after the two-core splits:

| case | dry | recording | room |
| --- | --- | --- | --- |
| VA MORPH PAD 8 notes + PUNCH BASS (VA.md "Two cores", 3y177) | 2,103 | 2,287 | 615..800 us |
| FM6 TINE EP 8 notes + DEEP SUB (3y187, package C) | 2,012 | 2,124 | 778..890 us |

What Bluetooth adds, on paper: two interrupts a connection event (every 7.5..11.25 ms, so one event every 2.6..3.9
halves), each at most 36 us at 96 MHz in Optimist's emulator, about 10..15 us at the FM-1's clock if the code is in
the cache, more when it is not; AES on an encrypted link. **Roughly 5..20 us a half on average, 40..80 us in the
worst half**: inside the room above. The interrupts sit below the audio, so they cannot make a half late by
themselves. What can:

1. **The radio start holds interrupts** (the RF program, and ~160 ms of calibration without stored trims): only at
   boot before the audio starts, or when nothing sounds (Melodee's rule, kept).
2. **Flash writes** (ChoralRoot's saves, the loop slots, the pool) run with interrupts off; the radio engine keeps
   time alone and the central retransmits, but a long sector erase starves the link layer. FIZZ lost Mac links to
   25..90 ms stalls (reason 8) until it held erases while a computer was connected.
3. **The flash cache**: the BLE handlers run from XIP and push audio code out of the cache. Unmeasured anywhere;
   the reason for the unit measurements in step 3.
4. **Core 1**: the stack runs on core 0 only; core 1 must sit in its RAM loop through the radio start, as at boot.
   The one-job protocol (DUALCORE.md) is not touched.

### 2.6 The flash map

- **FM-1**: the radio's trims are in the stock VM at **0xE8000** (records 106/107/108/187), and BTIF (stock pairing)
  at 0xE9000. ChoralRoot never writes 0xE5000..0xFBFFF (`FL_RANGE_OK` allows FL_DATA 0x97000..0xDFFFF, the OTA
  staging 0xE0000..0xE4FFF and the globals 0xFC000..0xFEFFF), so the trims survive on units that went stock ->
  ChoralRoot. Units that ran FIZZ (its first blob at 0xE5000/0xE7000) or SLOOP 2.4 may have lost them: the
  self-calibration covers those. ChoralRoot's own pairing record goes in its settings object (as Melodee's trailer:
  address 6, flags, bond 28, trims copy 100, device 70 B): **no new flash area**.
- **The second flash chip** (0xB36014, HISTORY 3y222): the stack reads flash only through ChoralRoot's driver
  (`FL_JEDEC_OK` already accepts it); the trims are the stock firmware's, written by the same factory flow. Nothing
  chip-specific; test on such a unit anyway (step 3).
- **SMK-37**: ChoralRoot's SMK store (FL_DATA 0x9D000..0xBAFFF) **is** the stock VM, so the SMK's trims are gone
  once ChoralRoot has written its store; only the self-calibration remains, and nobody has run it on an SMK. BTIF
  (0xC0000) is protected and stays unused.

## 3. How it fits ChoralRoot's MIDI

ChoralRoot's MIDI is Felucca's shape, as Melodee's is: `usb.c midi_enqueue(pkt, source)` into `midi_in_q`, drained
in the audio interrupt (`cr_out.c cr_midi_in`); the FM-1's own output through `midi_out_event` into `midi_out_q`.
Melodee's glue plugs into exactly those two places, so the port is close to one-to-one.

| ChoralRoot | over Bluetooth | over the USB host |
| --- | --- | --- |
| MIDI Perform (ch 1) in / out: the chord sound | in: plays the chord sound; out: the chord or the performance mode's notes | the same |
| MIDI Bass (ch 2) | in: the bass sound; out: the bass line | the same |
| MIDI Raw Chord | out | out |
| MIDI Keys (notes 53..79 press the keys) | in | in |
| program change, CC 7 / 91 / 93 / 94, CC 64, bend, CC 1, pressure | in | in |
| MIDI Clock Out: clock, start, stop | out (timed by its queue, ahead of notes) | out |
| MIDI Clock In | in, each pulse placed by its BLE timestamp, so 3y224's loop lock sees evenly spaced pulses; duplicates within 2 ms (two sources) are already dropped | in |
| the looper | records BLE notes as USB notes (the bass notes), plays back to every output | the same |
| SysEx (the editor, the device manager, backups, `perf_unit.py`) | **no** in version 1 | no (Melodee ignores it) |
| updates | **never** | never: USB HOST is off at every boot |

One rule to keep from Melodee: notes a peer left on are released when its link goes (any reason) or when
Bluetooth is switched off; USB and TRS notes keep sounding.

**Two ChoralRoots.** Over the USB host: A on USB Host with Clock Out, B on Clock In; B's loops follow A's (3y224's
lock); B plays A's chords on its own Perform channel (a layered second sound), or B listens on other channels to
stay independent. Over Bluetooth this needs the central role on one unit (step 6) and no hub.

### 3.1 The Options it needs

| Option | values | default | notes |
| --- | --- | --- | --- |
| **Bluetooth** | Off, On (step 6: Off, Visible, Connect) | Off | saved; On starts the radio at boot before the audio, or at once if nothing sounds, else "STOP SOUND FIRST" |
| **Bluetooth Name** | (shown) `ChoralRoot 1A2B` | | the value line also says Waiting / Connected / PIN 000042 |
| **Forget Bluetooth** | an action, with OK | | clears the bond and the remembered device, keeps the address and the trims |
| **USB Host** | Off, On | Off | **never saved**: every power-on is a USB device; the header of the Options pages says HOST while on |

The status (connected, waiting, a PIN to type) shows on the Options page and as a toast (BLUETOOTH CONNECTED /
LOST), the way the battery shows only on the Options pages. No new screen until step 6, whose device picker is the
one new screen (a picker, one device big, its neighbours peeking, as every ChoralRoot choice):

```
+--------------------------------+
|  CONNECT                    BT |
|                                |
|        microKEY-37  -48         |   <- the neighbour above, dim
|   >  Melodee 3F1C            <  |   <- the device, big; its signal strength
|        CH-1 BLE                 |   <- the neighbour below, dim
|                                |
|  K2 turn   OCT+ connect  OCT- back |
+--------------------------------+
motion: the list scrolls like the sound picker; a found device slides in; connecting pulses the name
```

### 3.2 The SMK-37

The same AC791N / WL82 (genkigenki's fm1-to-smk37 `docs/01-hardware.md`), the same SPL and loader; the stock SMK
labels its SAVE key "BT", so stock very likely does Bluetooth MIDI. Unknowns: the antenna and the trims (2.6). Its
XIP has 14.6 KB free, the same problem as the FM-1's; the resource region would need the SMK's own place (its map
differs). **Not in version 1.**

### 3.3 The web installer and the Sound Manager over Bluetooth

Chrome's Web MIDI sees a BLE-MIDI port once the operating system has connected it (macOS Audio MIDI Setup), so a page
could in principle talk SysEx to the FM-1 over Bluetooth. At Melodee's 27-octet packets that is about 6 KB/s
(RemoteFM1 measured the same limit with JieLi's stack; ~35 KB/s with data-length extension, 2M and MTU 247, on
fm1-base). A 70 KB backup would take ~15 s. **Installs and updates stay USB-only** (the loader and stub speak USB;
an interrupted wireless update is a brick risk). The Sound Manager over BLE is a later option, not in version 1
(decision 8).

## 4. Port or write anew

| piece | from | how |
| --- | --- | --- |
| the BLE stack (`src/ble/*`) | Melodee nightly 3fe570d (Optimist 498a3ae4 + Melodee's fixes: the initial read, bounded security settling, exact CCC acknowledgement, POOL placement, the central reconnect) | **port**, files unchanged where possible, notices kept; watch Optimist upstream for driver fixes |
| the WL82 driver, `hal/fm1_ble.h`, `fm1_ble_rf.h`, the radio program | Melodee (Optimist) | **port**; ChoralRoot's register check wants register access in `hal/` only, as Melodee's does |
| `hal/fm1_rf_cal.h` | Melodee (Charles Vestal) | **port** with its Apache-2.0 notice |
| the glue (`midi_ble.c`) | Melodee | **port and adapt**: `midi_enqueue` source 3, `midi_out_event`, ChoralRoot's settings object instead of Melodee's PER5 trailer, `cr_bootguard.h` instead of Melodee's bootguard |
| the screens, Options, toasts | | **new**, ChoralRoot's look (MOD palette, pickers) |
| the resource region | Melodee's `res.c` idea | **new** for ChoralRoot's loader, package and map (ChoralRoot's loader and package differ) |
| the USB host (`usb_host.c`, `usb_host_desc.h`) | Melodee nightly | **port**; its `usb.c` hooks map to ChoralRoot's `usb.c`, which is Felucca's too; the menu becomes an Option |
| the tests | Melodee `tests/ble_*` (protocol, crypto, fake radio, central), `usb_host_test.c` | **port** into `tests/run_all.sh`, under ASan/UBSan |

## 5. Risks

1. **The radio may not work on every unit.** Melodee issue #120: VISIBLE invisible and scanning finding nothing on
   one 1.6.1 unit. The driver was written from a fact sheet and has been played on few units ("other units and
   peers remain largely untested"). Step 0 tries it on the owner's units first.
2. **Audio glitches from the radio.** The start-up holds interrupts (kept to boot or silence); flash writes starve the
   link (hold store erases while connected, as FIZZ does); cache pressure on the heavy pads (measured in step 3
   against 2.5's table).
3. **The dual core.** The stack is core 0's; the radio start must find core 1 in its RAM loop; the BLE interrupts must
   not run inside the core-1 hand-over's critical sections (they are lower priority and masked where the audio
   masks). ThreadSanitizer-style host tests do not cover the radio; the emulator does not model the RF side.
4. **The boot and the update path.** The radio must never start in SAFE MODE, update mode or the loader; a start that
   hangs ends in the watchdog, and `cr_bootguard.h` must then keep the radio off for the next boot (Melodee's
   safety net). The resource region adds a loader record: the loader ships in every package (staged at 0xE0000) and
   the boot stub does not change. A region missing or from another build must leave the FM-1 able to show a message
   and take an update (S, the 12 px font, stays in XIP for that).
5. **Retiring the legacy stores** (decision 2): a unit still on 0.21 or older that jumps straight to this release
   would lose its pre-0.22 user sounds; the installer can send such a unit through 0.29.2 first.
6. **Reconnects.** macOS does not reconnect a BLE-MIDI device by itself after a firmware change (RemoteFM1); a Mac
   that saw "Melodee" or "FM-1" keeps old names (CoreMIDI). The address must stay the same across updates.
7. **The radio program's provenance** (1.4, decision 5).
8. **USB host power.** Not every hub or adapter will do; the direct C-to-C cable between two FM-1s may never work.

## 6. The build plan

Each step is its own branch and commit series with its size line before and after; nothing goes to the unit except
through the main session's flash rules.

**Step 0. Try the radio on the owner's units (no ChoralRoot code).** With the owner's go: Melodee 1.6.1 on a spare
FM-1 (not the ChoralRoot dev unit; backup first), VISIBLE in Audio MIDI Setup, play both ways; the same on a unit
with the second flash chip if one is at hand; then back to ChoralRoot. If the radio fails here, stop and report.

**Step 1. The resource region.** `app.ld` gets a RES region at XIP 0x0208F000 (48 KB, read-only data only); the
fonts M, CRB, CRX and the 16 px icons move there; `fm1pkg_make.py` adds the region as a record; the loader writes it
(`FL_LDR_RANGE_OK` gains 0x93000..0x9EFFF; `FL_DATA_LO` becomes 0x9F000); the app checks magic, length, CRC-32 and
the app's build hash at boot and otherwise draws a "FINISH UPDATE" screen in S; `storage.c` stops reading the
retired legacy objects. *Tests*: the 313 pixel-exact draw goldens and the regress goldens unchanged; emulator boots
with the region missing, stale and good; install, downgrade to 0.29.2 and back; an interrupted install. *Unit*:
install from 0.29.2, a UI frame-time check, boot time. *Expect*: XIP about -42.7 KB. Releasable alone.

**Step 2. The stack, built but off.** The files of section 4 behind `CR_BLE` (default 0: the image byte-identical);
the ported host tests. *Measure*: the size line with `CR_BLE=1`, peripheral only.

**Step 3. Bluetooth MIDI, the FM-1 as peripheral.** The glue, the Options, the settings fields, the boot rule, the
note release on loss, routing as section 3. *Tests*: routing (`cr_midi_test` with source 3), the settings
migration both ways, the UI renders. *On the unit*: a Mac and an iPhone connect and play both ways; reconnect after a
power cycle; `perf_unit.py va fm6 --seconds 20` dry and `--record` with Bluetooth off, advertising, and connected
with a 30-notes-a-second stream (block, t5, late, shed against 2.5's table: the bar is no late half and no shed);
the BLE interrupts' worst time (a debug readout, as Optimist's `isr_max_us`); the radio start's hold time; a
30-minute soak with saves and loop writes (link drops counted); a unit with the second flash chip; round-trip
latency through a Mac.

**Step 4. Clock over Bluetooth.** Out with Clock Out; in with Clock In, timestamps applied. *Unit*: 3y224's 10-minute
lock test with a Mac master over Bluetooth.

**Step 5. USB host.** `usb_host.c` ported, Options > USB Host. *Unit*: a keyboard through a powered hub; two
ChoralRoots through a powered OTG adapter and through a plain C-to-C cable (record which works); clock lock between
them; USB audio and updates back after Off and after a power cycle.

**Step 6. Connecting out** (only with 8 KB or more left after steps 1..5). The central role, the picker screen,
the passkey; a wireless link between two ChoralRoots; BLE keyboards.

**Step 7. The SMK-37.** Its own region place, the self-calibration on a unit, an antenna check.

## 7. The cost, in one table

| feature | XIP | RAM | POOL | RAMTEXT | fits now? |
| --- | --- | --- | --- | --- | --- |
| free today | 15,800 | 17,712 | 4,380 above the 8 KiB rule | 2,404 above the rule | |
| Melodee's whole Bluetooth (measured) | +49,908 | +3,464 | +4,792 | +36 | no |
| Bluetooth peripheral with bond (estimated from it) | ~+42,400 | +3.4 KB | +4.8 KB (or RAM +5.4 KB, POOL +2.9 KB with the glue state in RAM) | +36 | no |
| ChoralRoot's screens and Options | ~+1,500 | small | | | |
| USB host (estimate) | +5..8 KB | +0.4..0.6 KB | | | yes |
| the resource region (step 1) | **-42,697** (radio program then outside too: -5,795 from Bluetooth's figure) | 0 | 0 | 0 | |

## 8. Decisions for the owner

1. **One package, or a separate Bluetooth package?** *Recommended: one package*, made to fit by the resource
   region (2.4 A). Everyone gets Bluetooth and the host mode, releases stay one build per board, and nobody loses
   storage or sounds. The separate package (FIZZ's route) doubles every release and costs loop slots or an engine.
2. **Retire the pre-0.22 stores and their migration** to make the region? *Recommended: yes*, with the installer
   sending units older than 0.22 through 0.29.2 first.
3. **Which features first?** *Recommended*: the region (step 1), then Bluetooth with the FM-1 as peripheral (steps
   2..4: Macs, iPads, phones, DAWs), then USB host (step 5), then connecting out (step 6). The alternative is the
   USB host first: it fits today without the region, but it needs a powered hub and helps only players with two
   FM-1s or a USB keyboard.
4. **Port from Melodee's copy or from Optimist upstream?** *Recommended: Melodee's* (pinned at 3fe570d): its glue
   already matches ChoralRoot's Felucca MIDI plumbing and it carries the fixes found on units; credit Optimist,
   Melodee and Charles Vestal; follow Optimist for driver fixes.
5. **The radio program from stock V15** (no stated licence): ship it in the region as Melodee and Optimist do, or
   not ship Bluetooth? *Recommended: ship it*, listed in LICENSING.md with the same provenance note Melodee gives.
6. **Pairing.** *Recommended: Just Works bonding as peripheral* (a Mac remembers the FM-1 and reconnects without a
   prompt); the passkey only with step 6.
7. **The radio's default and start.** *Recommended: Off by default, saved; started at boot before the audio, or at
   once only when nothing sounds.*
8. **What crosses Bluetooth in version 1.** *Recommended: MIDI only* (notes, CCs, program changes, clock). No
   SysEx (no editor, device manager or backups) and never updates; the Sound Manager over BLE can come later.
9. **The radio's speed.** *Recommended: keep Melodee's 27-octet packets* for version 1 (tested on units); data-length
   extension and 2M later, measured (they matter for SysEx, not for notes).
10. **The SMK-37.** *Recommended: FM-1 only in version 1*; the SMK after a radio test on an SMK (its trims are gone
    under ChoralRoot's store).
11. **Step 0.** May a session put Melodee 1.6.1 on a spare FM-1 (backup first, then back to ChoralRoot) to see the
    radio work before any of this is built? *Recommended: yes*; it is the cheapest way to learn whether the
    driver works on these units (Melodee #120).

## 9. Sources

Melodee (scratch clone, nightly 3fe570d): `docs/BLE_MIDI.md`, `docs/BLE-STACK.md`, `BUILDING.md` "USB MIDI host",
`docs/RELEASE_1.6.0-rc.6.md`, `firmware/src/midi_ble.{c,h}`, `firmware/src/ble/`, `firmware/src/usb_host.c`,
`firmware/src/res.c`, `tools/fm1pkg_make.py`; issues #89, #120; PRs #75, #90, #112. Optimist: w0ts/optimist README
and LICENSING.md. FIZZ: `firmware/BUDGET.md` "The Bluetooth package". RemoteFM1: the `ble-radio` branch and the
memory note on fm1-base's radio settings. ChoralRoot: `docs/INTEGRATION.md` "Measured on the unit", `docs/VA.md`
"Two cores", HISTORY 3y177 / 3y187 / 3y222 / 3y224, `docs/FLASH.md`, `docs/BOOT-SAFETY.md`,
`firmware/hal/fm1_flash_map.h`, `firmware/src/storage.c`, `docs/MANUAL.md` section 15.
