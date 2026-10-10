# ChoralRoot FM-1: USB audio

ChoralRoot has Melodee's USB audio recording (Melodee 0.11.1: "Melodee Out plays the computer through the FM-1, Melodee
In records the four tracks as separate channels; each can be switched off"), renamed and re-cut for ChoralRoot's two
parts: **ChoralRoot In** records the master, the CHORD part and the BASS part as three stereo pairs. It is a
class-compliant USB Audio Class 1 device: no driver on macOS, Windows 10 / 11, Linux, iPadOS. Melodee's playback
device (the computer's audio played through the FM-1; "ChoralRoot Out" in the 0.14 dev builds) is **not** in
ChoralRoot: it was removed in 0.14 (below, "Removed: the playback").

This is phase 3 of the Melodee platform work (phase 1: FM6, [FM6.md](FM6.md); phase 2: the CZ-1 engine,
[CZ1.md](CZ1.md)).

**Names, from the FM-1's point of view.** The Options entry is **USB Record**: the computer records the FM-1. The
device the computer lists keeps the computer's point of view, as DAWs label their inputs: **ChoralRoot In**, an
input of the computer.

## What the computer sees

One USB device, **ChoralRoot FM-1** (maker "ChoralRoot"; VID 0x1209, PID 0x0001 as before), with two functions:

| Function | Name | What | Format |
| --- | --- | --- | --- |
| MIDI | **ChoralRoot FM-1** (the MIDI port) | USB-MIDI in and out, as before | |
| Audio input | **ChoralRoot In** | six channels to the computer | 6 channels, 44.1 kHz, 16 bit |

In Audio MIDI Setup (macOS) or Sound settings (Windows) it appears as "ChoralRoot In"; a DAW sees a 6-channel input.
The FM-1 offers no audio output to the computer.

The MIDI port is now called **ChoralRoot FM-1** (it was "Felucca"). The installer (`tools/fm1_install.py`,
`web/fm1ota.js`) matches "FM-1" and the web editor (`web/editor.html`) matches "ChoralRoot" as well as "Felucca", so
both keep finding the instrument. The device's `bcdDevice` is 3.22 with the recording and 3.21 with the console
(below), so a host never reuses descriptors it cached for Felucca (3.11), Melodee (3.06), the 0.14 dev builds'
playback + recording (3.20) or the other presentation.

### ChoralRoot In: the channels

| Channels | Name | The signal |
| --- | --- | --- |
| 1-2 | Master L / R | the master output after the limiter and the speaker EQ (Felucca's LOWCUT / BASS+ when set), as the DAC gets it, **without** the metronome click. Its level follows MASTER, or not (USB Level, below) |
| 3-4 | Chord L / R | the CHORD part (part 0: the chord sound, the RAW chord's when it plays part 0) as it goes into the mix: after its DIST, LEVEL, pan and the stereo spread (VA's SPREAD), **before** the sends, the FX buses (chorus, delay, reverb), the FX layer and MASTER; at **half level (−6 dB)** |
| 5-6 | Bass L / R | the BASS part (part 1), the same way |

The part pairs are dry: the shared FX buses cannot be split by part. They are taken at −6 dB because a part alone may
be louder than the master's full scale before the limiter (a six-note chord at a high LEVEL reaches about twice the
limiter's threshold); all channels saturate at 16-bit full scale. Capture is 16 bit only: the mix is 16-bit, and a
24-bit format would only pad a zero byte (and need 810-byte packets instead of 540).

Hosts that read UAC1 channel names (the input terminal's `iChannelNames`, strings 4..9) show "Master L" .. "Bass R";
Windows numbers them 1..6.

### Removed: the playback

The 0.14 dev builds also had Melodee's playback device, "ChoralRoot Out" (the computer's stereo added after the master
limiter, x MASTER, with Options > USB Audio Out). It is gone: its interfaces, its OUT endpoint (EP2 OUT) and explicit
feedback endpoint (EP3 IN), its 4 KiB ring in the POOL, its packet buffers and the mix-in after the limiter. Its
settings byte is reserved (docs/SETTINGS.md, version 5). USB MIDI is unchanged.

## Options

Two entries in Options (GLO tap), after Hold Time, one setting per screen; KNOB 1 sets them. The line under the value
says what each does:

| Option | Values | Default | What it does |
| --- | --- | --- | --- |
| **USB Record** | On / Off | On | the computer can record the FM-1: presents ChoralRoot In (the line: "records master, chord, bass") |
| **USB Level** | Master / Fixed | Master | Master ("record level: MASTER knob"): ChoralRoot In's master pair follows the MASTER knob. Fixed ("record full, MASTER: speaker"; Felucca 1.0.5's MENU > USB LEVEL FIXED, #42): the mix goes to the limiter at the full MASTER level, ChoralRoot In records that, and only then does MASTER scale what the DAC (speaker, headphones) gets, so you can record at full level with the speaker turned down |

(The value texts stay "Master" / "Fixed": "Fixed (record at full level)" is wider than the picker's value line.)

USB Level changes at once. USB Record changes **what the computer is given** (Melodee's mechanism: the configuration
is rebuilt): once the setting has rested for 0.6 s (so stepping through Off and back costs nothing), the FM-1 leaves
the bus for about a second and connects again; the screen says "USB RECONNECTING", and the computer reads the new
configuration. The MIDI port disappears for that second too. The settings are saved with the others
(docs/SETTINGS.md, record version 5) and the device presents itself that way from power-on.

**The serial console** is available when **USB Record is Off**, or in **SAFE MODE**. The recording and the USB serial
console (`console.c`, the `cpu`, `boot`, `status` commands; docs/INTEGRATION.md "Performance") both need EP2 IN (the
capture's isochronous stream, the console's notification endpoint), so the firmware presents one or the other: with
USB Record Off, MIDI and the **console** (as earlier builds always did); with it On, MIDI and the recording. **SAFE
MODE** always presents the console (no audio function: `UAC_BLOCKED`, `core.h`), so a crashing boot can be read over
the console. Removing the playback freed EP2 OUT and EP3, which the console's data could take, but the two still do
not go together: the console's notification endpoint would need an IN endpoint other than EP2 (only EP4 is left,
whose DMA registers differ and which only Felucca's isochronous stream ever used), and, decisively, Felucca's #67
(fixed in Felucca 1.0.5 by its MENU > USB SERIAL): macOS 13 to 15 attach Apple's CDC composite driver to a device that
has a CDC function, and their audio driver then never takes the audio interfaces. ChoralRoot's recording presentation
has no CDC function at all (Melodee's layout), so the switch stays.

## Limits

- **44.1 kHz only**, no sample-rate conversion: the computer must run the device at 44.1 kHz (macOS and Windows do so
  for a device that offers only that rate; a DAW project at 48 kHz resamples or refuses, depending on the DAW).
- **Clock:** the FM-1's I2S clock (~44,117.6 Hz) is not locked to USB. The endpoint is asynchronous: capture packets
  follow a fixed 44.1-frame pattern (44 frames, a 45 every 10th packet); when the ring's fill leaves 128..384 frames
  a packet takes one frame more or fewer (a 44 made 45 or a 45 made 44, at most one in 10 packets). Nothing is
  resampled; the ring absorbs the difference.
- **Latency** (on top of the computer's buffers): ~5.8 ms (the ring holds 256 frames). A ring that runs over (the
  host stops reading) or dry (a 45 ms flash erase when a setting or a sound is saved) is counted and re-primed: a
  short gap, never stale or repeated audio.
- **Bus bandwidth:** the packets carry up to 540 bytes; the endpoint reserves 640 a frame (bit stuffing: "What went
  wrong and why" below) of a full-speed frame's 1500, inside a USB 2 hub's split-transaction budget (~1157). (Six
  channels at 24 bit would have been 810.)
- **The metronome click** is not in ChoralRoot In.
- **No playback:** the computer cannot play through the FM-1 (removed, above).
- **The emulator has no USB:** the Options entries are there and saved, nothing streams (`FELUCCA_UAC` is 0 in the
  emulator's build, `tools/emu/emu_firmware.h` through `tests/hostsim.c`).
- **Felucca's single stereo input** (the master on EP4, FELUCCA_UAC 1.0) is gone: ChoralRoot In's channels 1-2 are
  the same signal.

## Cost

The emulator cannot stream, so the cost is measured on the host and scaled (`sh tools/emu/perf.sh`, scenario (u):
`tests/cr_usbaudio_test.c --bench`, emu.c's ratio of 259 host instructions per device µs): while the computer
records, the audio ISR's share (the stage cleared, the part captures, the master tap, the ring copy) is about 21 µs a
2.9 ms half and TIMER5's packets (the packet out) about 10 µs: **~31 µs, 1.1 % of the half**, plus the SIE
register accesses of the endpoint service (up to 4 kHz, nested in the render; not in the host figure). Without the
stream the ISR does none of it (a flag test a block, no IRQ-off section). On the device: record, then switch USB
Record Off and read `cpu` and `status` on the console: `audio_max_all_us`, `ua_poll_max_us`, `ua_service_max_us`
(the longest endpoint service), and the glitch counters.

## Memory

| Buffer | Where | Size |
| --- | --- | --- |
| capture ring: 512 frames x 6 ch x 16 bit | POOL (`usb_audio_stream.c` `ua`) | 6144 B |
| the stream state and counters | POOL | 44 B |
| capture packets, double-buffered: 2 x 540 B | RAM (`usb_audio.c` `ua_tx`) | 1080 B |
| the configuration as sent (`ua_cfg`) | RAM | 208 B |
| one block's capture frames, 32 x 6 x 16 bit | RAM (`fx.c` `ua_stage`) | 384 B |

Felucca's master-only input (a 2 KiB ring and its packet in RAM) is removed. Removing the playback, measured by
`./build.sh` against the 0.14 dev build with it: POOL −4,124 B (321,612 → 317,488 B, 92.3 %), RAM −432 B (79,516 →
79,084 B, 80.4 %), XIP −1,272 B (349,200 → 347,928 B).

## Verified, and what only hardware can verify

Host tests (`tests/cr_usbaudio_test.c`, in `tests/run_cr_tests.sh` and `tests/run_tests.sh`): the descriptors of
both presentations (USB Record on, the console) parsed as a host parses them, no playback (no OUT or feedback
endpoint, no stereo format); the routing (the six staged channels into the ring, none once the host closed the
stream); the PCM16 packing; 20 s of a fast (44,117.6 Hz) and a slow (44,070 Hz) I2S clock against the host's frames
with renders at random times: no glitch, every frame once and in order, the rate matched, the ring's fill well inside
it; the host stopping for 50 ms: counted, re-primed, in order again. The settings: `tests/cr_settings_test.c`.

Only the device can show: **enumeration on macOS and Windows** (the recording device and the MIDI port appear, the
names, 44.1 kHz accepted; the console with USB Record Off, the replug), long-run **clock drift** against a real host
(no `ua_cap_underruns` / `_overruns` over an hour), the 540-byte capture packets on the JieLi controller (Melodee's
largest was the same 540), the stream with a full chord (`audio_max_all_us`), and iPadOS.

## What went wrong and why (0.14, 2026-10-10)

The first device recordings (QuickTime, ffmpeg, Live) gave one slice of sound and then seconds of silence: a 14 s
capture held 6.5 s of audio. GEEK OUT's `usb` line showed the ring over-running (`o` rising, `u` 0) only while notes
sounded. Measured with `tools/uac_check.py` and the device's counters (`fm1_install.py --debug`, below), one step
at a time:

- **The device was not starved.** The endpoint is armed again as soon as the host takes a packet: at every call of
  the service, from TIMER5 (every tick, nested in the render too) and from the audio ISR before each of its blocks,
  with the IRQs off (`main.c ua_service_paced`, `audio.c ua_isr_service`). With that, ~992 packets a second went out
  while the stream was open, every gap between two packets under 1.2 ms.
- **The host closed and reopened the stream.** The event log showed `SET_INTERFACE` alt 0 about 250 ms after each
  alt 1, then alt 1 again: ~25 times in 14 s with chords, ~6 in silence, nothing on the device's side between. Each
  reopen cost the recording a restart. The packet rate (a servo, an exact 44.1 pattern, the I2S's own 44.1176), the
  sync type (async, adaptive, synchronous), the CPU load and the MIDI traffic made no difference.
- **The sample bytes did.** Builds that replaced the audio in the packets: all zeros with chords playing, 1 reopen
  (0.938 of the wall time recorded); constant 0x1000, 4; noise, 6; the master pair only (the part channels zero), 19;
  the real chords, ~25; every sample 0xFFFF (silent to the ear), 29, the worst. USB inserts a 0 bit after six 1 bits
  (bit stuffing), so a packet of 0xFF bytes is up to 7/6 longer on the wire; small negative samples (0xFFxx), all over
  quiet and decaying audio, do the same. The endpoint reserved exactly 540 bytes a frame, and the FM-1 was on a
  USB 2 hub (split transactions, a tight schedule): a stuffed packet overran its slot, the host counted an error and
  CoreAudio restarted the stream.
- **The fix:** `wMaxPacketSize` 640 (`usb_audio_stream.c UA_EP_MAXP`; 540 x 7 / 6 = 630 covers the worst case), the
  packets unchanged. On the device: 12.7 and 13.2 s of 14 recorded with chords, 27.9 s of 30, the stream opened once
  and held. A port straight on the computer may have held with 540; 640 costs nothing (a full-speed frame holds 1500
  bytes).
- **Kept from the hunt:** the ring is not fed until the host takes its first packet (`ua.cap_hold`: macOS reads ~14
  ms after `SET_INTERFACE`, which over-ran the 11.6 ms ring at every open) and is held again if the host stops
  reading for 20 ms; the fixed packet pattern (Melodee's fill servo wandered around 44.07 frames a packet after every
  ring reset); the endpoint armed from the audio ISR too, under `usb_sie_owner` (`usb_audio.c`: no two contexts in
  one SIE register sequence, `INDEX` then the indexed registers); the counters and the event log below.

## Reading the counters during a recording

`python3 tools/fm1_install.py --debug` (USB-MIDI SysEx 73, read-only, also while recording; web/EDITOR_PROTOCOL.md)
prints the device's counters by name, the mean packet length of the last whole open, and the last 16 events (each
`SETUP` the host sent, by name; a stalled one; each ring over-run) with the device's time in ms. Take one before and
one after a recording; the differences say:

| field | healthy |
| --- | --- |
| `ua_opens`, `ua_open_close_ms` | one open per recording, held for its length (each reopen is a gap in the recording) |
| `ua_tx_packets` / `ua_open_ms` | ~1 packet a ms while open |
| `ua_overruns`, `ua_underruns` | 0 after the first open (an over-run: the host stopped reading; an under-run: the ring ran dry) |
| `ua_arm_gap_*` | the gaps between two armed packets: all `< 1.2 ms` but one per open |
| `ua_pk528`, `ua_pk540`, `ua_pk_other` | ~90 % / ~10 % / 0 (the 44.1 pattern; any other length is a bug) |
| `ua_first_take_ms` | ~14: the host's first read after an open |
| `ua_holds` | 0 (the host stopped reading without closing the stream) |
| `ua_isr_sie_busy`, `ua_t5_sie_busy` | 0 (an SIE sequence was preempted and its service skipped) |
| `nested`, `late`, `cpu_*`, `st_*` | the audio ISR: TIMER5 nested in it, late halves, the render's time and stages |

## Measuring the recording on a Mac

`tools/uac_check.py OUT.wav [SECONDS] [--nonotes] [--debug]` records ChoralRoot In with ffmpeg (avfoundation) while
it plays C-major chords on the FM-1 over USB-MIDI (mido), then prints the WAV's length against the wall time (the
drain ratio: ~0.93 or more is healthy, ffmpeg's start costs ~1 s), the loud 50 ms windows and every silent gap of
0.5 s or more while chords were sent; `--debug` adds the device's counters before and after. Needs `ffmpeg`, `mido`
and `python-rtmidi`; it never writes the FM-1.

## Where it lives

| File | What |
| --- | --- |
| `firmware/src/usb.c` | the descriptors (`CFG_DESC` with `usb_audio_desc.h`, `CFG_DESC_CDC`), the strings, EP0 (SET_INTERFACE, the UAC1 sampling-frequency requests), `usb_cdc_on`, `usb_replug`, `ua_off_set` / `ua_off_apply`, `ua_service` |
| `firmware/src/usb_audio.c` | Melodee's endpoint service: `ua_cfg_build`, SET_INTERFACE, the isochronous endpoint in TIMER5 |
| `firmware/src/usb_audio_stream.c` | the capture ring, the packet pattern and its nudge, `UA_EP_MAXP`, the packet format, `ua_audio` (no hardware: the host test builds it) |
| `firmware/src/usb_audio_desc.h` | the recording function's descriptors |
| `firmware/src/fx.c` | `ua_stage` and the part captures (`mix_part`), `fx_usb_fixed` / `usb_fixed_dac` (USB Level) |
| `firmware/src/choralroot.c` | the master tap (the `mix_block` shim, before the click) |
| `firmware/src/audio.c` | USB Level Fixed's DAC scaling, then `ua_audio` with the IRQs off |
| `firmware/src/main.c` | `ua_service` in TIMER5 (every 250 µs at most, nested in the render too); `ua_off_apply` in the main loop |
| `firmware/src/cr_settings.[ch]`, `cr_ui.c` | the two settings and their Options entries |
| `firmware/src/console.c` | `status` / `dbg`: the `ua_*` counters (since the boot) |

Build flags: `FELUCCA_UAC` (1: the USB recording) and `FELUCCA_CDC` (1: the console, presented as above). `FELUCCA_UAC=0`
gives the previous MIDI + console device (no audio). The update loader is unchanged (byte-identical).
