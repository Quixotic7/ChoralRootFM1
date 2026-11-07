# Bluetooth MIDI

Melodee includes a port of Optimist's bare-metal BLE stack for the FM-1's
AC791N/WL82 radio. One connection can run in either role, with MIDI input and
output together. USB MIDI, USB audio and TRS input remain available. Bluetooth
audio and simultaneous central/peripheral links are not included.

## Controls

Hold HOME, select **Bluetooth**, then confirm with OCT+.

- **Radio:** OCT+/SAVE toggles it; K1 right enables, left disables. The state is
  kept across power-on: a radio left on starts again at the first quiet moment
  after boot. Starting requires stopped music, released voices and idle USB
  audio, because radio initialization briefly holds interrupts; otherwise the
  page says **STOP AUDIO TO START BLE**. With the radio program or calibration
  missing it says **FINISH SETUP IN WEB APP** or **RADIO CALIBRATION MISSING** and
  stays off.
- **Mode:** **CONNECT OUT** scans for devices while this page is open and stays
  quiet elsewhere. **VISIBLE** advertises for a computer or phone, never scans
  and hides the device list; the header shows `VISIBLE 1A2B`. Kept across power-on.
  A link already up stays up; the mode applies when it ends.
- **Disconnect:** leaves the current connection and resumes discovery. It also
  ends the search for the last device.
- **Last device:** reconnects the last successful outgoing connection. Saved
  bonds and device identity survive reboot. With the radio on in CONNECT OUT, the
  FM-1 scans for 10 s every 30 s until the last device is seen, then connects, and
  re-arms after a dropped link. Disconnect, Forget pairings, VISIBLE, a refused key
  or pairing and an unanswered PIN end this.
- **Forget pairings:** asks for confirmation, disconnects, and clears the
  incoming bond and remembered outgoing device. It preserves this FM-1's own
  address, calibration and music. Forget Melodee on the peer too if necessary.
- Turn K2 or PRESETS to select a discovered BLE-MIDI device; OCT+/SAVE connects.
  OCT- returns to Settings and stops discovery, retaining an established link.

A computer or phone can connect to **Melodee XXXX** in VISIBLE mode. On a
Mac use Audio MIDI Setup's Bluetooth MIDI window; on a phone use a music app's
Bluetooth MIDI settings. Connecting out from Melodee finds a BLE controller,
synth, another FM-1, or an app advertising the BLE-MIDI service.

The shared header shows connection/setup state. **PIN 000042**, for example,
means enter all six digits on the other device, including leading zeros.
The code disappears on encryption, failure, cancellation or timeout. The
device is connected for MIDI only after its notification subscription is
acknowledged; a radio connection alone can show **BLE WAIT**. **FORGET ON PEER**
means the other device holds an old pairing: forget Melodee there and connect again.

Synth parts use existing channel/MPC routing. MIDI-only tracks can select BLE
through GLO > SYSTEM > MIDI; their output uses the active USB and BLE transports.
Performance channel messages are supported.

GLO > GLOBAL > CLK decides the clock. On **INT**, Melodee sends clock over BLE as
over USB, stopped or playing, with Start on PLAY and Stop on STOP; GLO + PLAY sends
Stop then Start, PLAY from a cursor on SONG sends Song Position and Continue, and
leaving INT sends one Stop. On **BLE**, Melodee follows incoming
Clock/Start/Continue/Stop and Song Position from the connection and sends no clock.
Song Position followed by Continue starts every pattern where playing from the top
would put it. Clock and transport wait in their own queue, ahead of notes.

Wireless firmware updates, editor commands and SysEx imports are not routed over BLE.

## Pairing and compatibility

The stack supports encrypted **LE legacy pairing**, with Just Works for peers
without a keyboard and a generated displayed passkey for peers that can type.
It does **not** implement LE Secure Connections (ECDH), numeric comparison or
multiple simultaneous links. A peer requiring Secure Connections fails rather
than being described as securely connected by that method.

Saadet's `platform/ble_client.h` and `docs/VALIDATION.md` informed the port:
initial MIDI characteristic read, fallback when reading is unsupported,
subscription acknowledgement as the success gate, five bounded 200-ms ATT
security-settling retries, no duplicate pairing while a code is being entered,
and clearing pending setup on cancel/timeout. Pairing uses Optimist's existing
DisplayOnly + bonding/MITM implementation; no JieLi SDK Bluetooth archive is
linked. A refused unauthenticated bond can use Optimist's authenticated repair
on the same link. A refused pairing or an unanswered code waits for another user
action; only a lost link or an unseen last device is looked for again.

The port uses the stock-tested 27-octet link-layer PDU limit. Data-length
extension remains disabled pending measurements on Melodee hardware.

## Calibration and persistence

The radio uses the device's trims (VM records 106, 107, 108, 187).
Initialization reads the stock VM candidates without writing them and keeps a
CRC-checked copy in Melodee's own settings. Some earlier Melodee preset writes
may have replaced the stock VM sectors. When no complete valid set exists, the
radio measures its own calibration each time it starts (`firmware/hal/fm1_rf_cal.h`,
about 160 ms): records 106, 107 and 108 are the SDK board's constants, the same
values stock firmware stores, and record 187 (the TX LO, RX DC and RX filter
trims, with the VCO bank table) is measured as JieLi's SDK library measures it.
Checked on an FM-1 whose VM was overwritten: the scan, the filter codes and the
PLL registers equal those the JieLi SDK build measures on the same unit, TX LO
and RX DC within noise, and BLE-MIDI connects and plays. A saved calibration,
imported below, replaces the measurement.

With this device's own full 1 MiB backup, inspect it first, then import just the
calibration through the connected FM-1 USB MIDI port:

```sh
build/venv/bin/python tools/ble_calibration.py /path/to/full-flash-before.bin
build/venv/bin/python tools/ble_calibration.py /path/to/full-flash-before.bin --restore --port FM-1
```

Keep Radio OFF, stop playback, release notes and close USB audio streams first.
The tool checks every VM record checksum, the RF record's inner checksum and
copy checksum. It imports only 100 bytes into Melodee's A/B settings and confirms
that the expected checksum was saved. It does not restore the flash image or
write any VM, project or preset sector. Different existing valid calibration is
protected against replacement. This is radio calibration, separate from the
knob/button calibration menu. The connected firmware must support USB editor
command 81 (version 1); earlier experimental BLE builds need updating first.

PER5 stays 604 bytes, and the optional template keeps its existing offset.
A tagged 212-byte BLE trailer stores address, the Mode and Radio flags, calibration,
incoming bond and the outgoing device/bond. Settings with a template use the previously unused
224-byte gap after the settings commit header, marked `BLE1` in that header.
Both old and compact copies remain readable during A/B conversion. Payload
CRC and commit-last semantics protect the whole record, including the trailer.
No flash partitions move. Saves wait for quiet audio; newer radio events
remain dirty if they arrive during a save.

Musical archives carry preferences/templates rather than device pairing keys.
Restoring one preserves this FM-1's current BLE identity and schedules a quiet
save to reattach its trailer. Earlier firmware does not understand the trailer;
keep this limitation in mind when testing downgrades with saved templates.

## Source provenance

Imported from [w0ts/optimist](https://github.com/w0ts/optimist/tree/498a3ae47c4f01a6c0cc891c794f8cd2ed78cd66),
commit `498a3ae47c4f01a6c0cc891c794f8cd2ed78cd66`:

- `firmware/src/ble/`: Optimist's protocol stack, crypto, controller, calibration
  reader and device store, GPL-3.0-only; original SPDX and source comments kept.
- `firmware/hal/fm1_ble.h` and `fm1_ble_rf.h`: Optimist's radio/baseband access,
  GPL-3.0-only. Local changes place buffers in POOL and replace the bounded RF
  replay recursion with equivalent iteration so the stack audit can prove it.
- `firmware/hal/ble_rf_tables.h`: copied from Optimist's
  `ble_rf_tables_v15.h`. This is stock M-VAVE V15-derived register/table data,
  **not independently authored Optimist or Melodee code**. Optimist's licensing
  record lists no stated upstream licence for these values. That provenance
  distinction remains; the code's GPL identifier does not relicense the data.
  Since 1.6 the radio program is stored in the resource region written from the
  update package, not in the application.
- `tests/ble_*` protocol fixtures and fake baseband, plus `docs/BLE-STACK.md`
  and `docs/BLE-HW-FACTS.md`: upstream tests and reference documentation.
  Those two documents describe Optimist, including features beyond this port.

Local adapter, UI, settings and integration-test code is Copyright (C) 2026
Kerem Kilic (Ellic Studio), GPL-3.0-only. Modified upstream files identify the
Melodee changes while retaining their Optimist provenance. Stack changes cover
the optional initial read, bounded security settling, exact CCC acknowledgement,
and SRAM placement. Saadet was consulted at local branch `test/native48-saadet`;
its SDK-specific helper was not copied into this stack.

## Validation

`python3 tests/ble_tests.py` runs protocol/crypto, encrypted peripheral pairing,
central passkey/bond recovery, the fake WL82 radio, calibration parsing,
bidirectional Melodee routing and UI fixtures under ASan/UBSan. An independent pinned upstream interpreter comparison checks the
iterative RF replay across 32 trim sets. Compact settings
tests interrupt every programmed byte and verify fallback to the previous copy.
The BLE UI goes through the existing layout/overlap/monochrome checker in both
fonts and every palette. This group also blocks the full host/web suite.

The target build checks flash/RAM limits, the radio crystal ramp's SRAM
placement and both BLE IRQ paths with nested audio, TIMER5 and fault handling.
These checks do not establish physical RF operation, sustained MIDI/audio
deadlines, reconnect behavior or compatibility on Melodee hardware. For 1.6.0,
Bluetooth connecting out to Cubasis on an iPhone was played on one FM-1; other
units and peers remain largely untested. Clock output over BLE has automated
coverage only (`tests/melodee_ble_test.c`).
