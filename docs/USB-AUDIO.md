# ChoralRoot FM-1: USB audio

ChoralRoot has Melodee's USB audio (Melodee 0.11.1: "Melodee Out plays the computer through the FM-1, Melodee In records
the four tracks as separate channels; each can be switched off"), renamed and re-cut for ChoralRoot's two parts:
**ChoralRoot Out** plays the computer through the FM-1, **ChoralRoot In** records the master, the CHORD part and the
BASS part as three stereo pairs. Both are class-compliant USB Audio Class 1 devices: no driver on macOS, Windows 10 /
11, Linux, iPadOS.

This is phase 3 of the Melodee platform work (phase 1: FM6, [FM6.md](FM6.md); phase 2: the CZ-1 engine,
[CZ1.md](CZ1.md)).

## What the computer sees

One USB device, **ChoralRoot FM-1** (maker "ChoralRoot"; VID 0x1209, PID 0x0001 as before), with three functions:

| Function | Name | What | Format |
| --- | --- | --- | --- |
| MIDI | **ChoralRoot FM-1** (the MIDI port) | USB-MIDI in and out, as before | |
| Audio output | **ChoralRoot Out** | the computer's audio, played by the FM-1 | stereo, 44.1 kHz, 16 or 24 bit |
| Audio input | **ChoralRoot In** | six channels to the computer | 6 channels, 44.1 kHz, 16 bit |

The two audio functions are separate devices, each on its own clock (as Melodee's: macOS times a single duplex device
from its recording packets, so a late one there costs playback). In Audio MIDI Setup (macOS) or Sound settings
(Windows) they appear as "ChoralRoot Out" and "ChoralRoot In"; a DAW sees a 2-channel output and a 6-channel input.

The MIDI port is now called **ChoralRoot FM-1** (it was "Felucca"). The installer (`tools/fm1_install.py`,
`web/fm1ota.js`) matches "FM-1" and the web editor (`web/editor.html`) matches "ChoralRoot" as well as "Felucca", so
both keep finding the instrument. The device's `bcdDevice` is 3.20 with the audio and 3.21 with the console (below),
so a host never reuses descriptors it cached for Felucca (3.11), Melodee (3.06) or another presentation.

### ChoralRoot In: the channels

| Channels | Name | The signal |
| --- | --- | --- |
| 1-2 | Master L / R | the master output after the limiter and the speaker EQ (Felucca's LOWCUT / BASS+ when set), as the DAC gets it, **without** the metronome click and **without** ChoralRoot Out's playback (no feedback loop when the DAW monitors its input). Its level follows MASTER, or not (USB Level, below) |
| 3-4 | Chord L / R | the CHORD part (part 0: the chord sound, the RAW chord's when it plays part 0) as it goes into the mix: after its DIST, LEVEL, pan and the stereo spread (VA's SPREAD), **before** the sends, the FX buses (chorus, delay, reverb), the FX layer and MASTER; at **half level (−6 dB)** |
| 5-6 | Bass L / R | the BASS part (part 1), the same way |

The part pairs are dry: the shared FX buses cannot be split by part. They are taken at −6 dB because a part alone may
be louder than the master's full scale before the limiter (a six-note chord at a high LEVEL reaches about twice the
limiter's threshold); all channels saturate at 16-bit full scale. Capture is 16 bit only: the mix is 16-bit, and a
24-bit format would only pad a zero byte (and need 810-byte packets instead of 540).

Hosts that read UAC1 channel names (the input terminal's `iChannelNames`, strings 5..10) show "Master L" .. "Bass R";
Windows numbers them 1..6.

### ChoralRoot Out: where the computer's audio goes

The host's stereo is added to the output **after** ChoralRoot's master limiter and soft clipper (Melodee's choice): it
is not limited, compressed or EQ'd by the instrument, and it does not pass through the FX. It is scaled by the MASTER
knob (so the knob sets the whole level of the speaker and the headphone / line out) and the sum saturates at the
DAC's 16-bit full scale (−6 dBFS of the 24-bit DAC, as everything ChoralRoot plays). PCM24 from the host keeps its top
16 bits. It never reaches ChoralRoot In.

## Options

Three entries in Options (GLO tap), after Hold Time, one setting per screen; KNOB 1 sets them:

| Option | Values | Default | What it does |
| --- | --- | --- | --- |
| **USB Audio Out** | On / Off | On | presents ChoralRoot Out to the computer |
| **USB Audio In** | On / Off | On | presents ChoralRoot In |
| **USB Level** | Master / Fixed | Master | Master: ChoralRoot In's master pair follows the MASTER knob. Fixed (Felucca 1.0.5's MENU > USB LEVEL FIXED, #42): the mix goes to the limiter at the full MASTER level, ChoralRoot In records that, and only then does MASTER scale what the DAC (speaker, headphones) gets, so you can record at full level with the speaker turned down |

USB Level changes at once. Out and In change **which devices the computer is given** (Melodee's mechanism: the
configuration is rebuilt without the functions switched off): once the setting has rested for 0.6 s (so stepping
through Off and back costs nothing), the FM-1 leaves the bus for about a second and connects again; the screen says
"USB RECONNECTING", and the computer reads the new configuration. The MIDI port disappears for that second too. The
settings are saved with the others (docs/SETTINGS.md, record version 4) and the device presents itself that way from
power-on.

**Both off: the serial console.** The audio functions and the USB serial console (`console.c`, the `cpu`, `boot`,
`status` commands; docs/INTEGRATION.md "Performance") share endpoints EP2 / EP3, so the firmware presents one or the
other: with USB Audio Out and USB Audio In both Off, the FM-1 presents MIDI and the **console** (as earlier builds
always did); with either On, the audio. **SAFE MODE** always presents the console (no audio function: `UAC_BLOCKED`,
`core.h`), so a crashing boot can be read over the console. This also keeps clear of Felucca's #67 (fixed in Felucca
1.0.5 by its MENU > USB SERIAL): macOS 13 to 15 attach Apple's CDC composite driver to a device that has a CDC
function, and their audio driver then never takes the audio interfaces; ChoralRoot's audio presentation has no CDC
function at all (Melodee's layout).

## Limits

- **44.1 kHz only**, no sample-rate conversion: the computer must run the devices at 44.1 kHz (macOS and Windows do so
  for a device that offers only that rate; a DAW project at 48 kHz resamples or refuses, depending on the DAW).
- **Clock:** the FM-1's I2S clock (~44,117.6 Hz) is not locked to USB. Both endpoints are asynchronous: capture packets
  carry 44 or 45 frames and one more or fewer as a low-pass ring-fill servo asks; playback tells the host its rate
  through an explicit 10.14 feedback endpoint. Nothing is resampled; the rings absorb the difference.
- **Latency** (on top of the computer's buffers): ChoralRoot In ~5.8 ms (its ring holds 256 frames), ChoralRoot Out
  ~11.6 ms (512 frames, Melodee's). A ring that runs dry or over (the host stops reading or sending, a 45 ms flash
  erase when a setting or a sound is saved) is counted and re-primed: a short gap, never stale or repeated audio.
- **Bus bandwidth:** Out at 24 bit 270 bytes a frame, In 540: 810 of a full-speed frame's 1500 bytes, inside a USB 2
  hub's split-transaction budget (~1157). (Six channels at 24 bit would have been 810 for In alone.)
- **The metronome click** is not in ChoralRoot In.
- **The emulator has no USB:** the Options entries are there and saved, nothing streams (`FELUCCA_UAC` is 0 in the
  emulator's build, `tools/emu/emu_firmware.h` through `tests/hostsim.c`).
- **Felucca's single stereo input** (the master on EP4, FELUCCA_UAC 1.0) is gone: ChoralRoot In's channels 1-2 are
  the same signal.

## Cost

The emulator cannot stream, so the cost is measured on the host and scaled (`sh tools/emu/perf.sh`, scenario (u):
`tests/cr_usbaudio_test.c --bench`, emu.c's ratio of 259 host instructions per device µs): with both streams running,
the audio ISR's share (the stage cleared, the part captures, the master tap, the ring copy and the playback mix-in)
is about 49 µs a 2.9 ms half and TIMER5's packets (in, out, the servo, the feedback) about 17 µs: **~66 µs, 2.3 % of
the half**, plus the SIE register accesses of the endpoint service (up to 4 kHz, nested in the render; not in the host
figure). Without a stream the ISR does none of it (a flag test a block, no IRQ-off section); with Out alone, no capture
staging. On the device: stream both ways, then switch both devices Off and read `cpu` and `status` on the console:
`audio_max_all_us`, `ua_poll_max_us`, `ua_service_max_us` (the longest endpoint service), and the glitch counters.

## Memory

| Buffer | Where | Size |
| --- | --- | --- |
| playback ring: 1024 frames x 2 ch x 16 bit | POOL (`usb_audio_stream.c` `ua`) | 4096 B |
| capture ring: 512 frames x 6 ch x 16 bit | POOL | 6144 B |
| the stream state and counters | POOL | 72 B |
| capture packets, double-buffered: 2 x 540 B | RAM (`usb_audio.c` `ua_tx`) | 1080 B |
| playback packet (270 B + DMA guard) and feedback | RAM | 280 B |
| the configuration as sent (`ua_cfg`) | RAM | 368 B |
| one block's capture frames, 32 x 6 x 16 bit | RAM (`fx.c` `ua_stage`) | 384 B |

Felucca's master-only input (a 2 KiB ring and its packet in RAM) is removed. Net, measured by `./build.sh`: RAM −124 B
(79,640 → 79,516 B, 80.9 %), POOL +10,312 B (311,300 → 321,612 B, 93.5 %).

## Verified, and what only hardware can verify

Host tests (`tests/cr_usbaudio_test.c`, in `tests/run_cr_tests.sh` and `tests/run_tests.sh`): the descriptors of
each presentation (both devices, Out only, In only, the console) parsed as a host parses them; the routing (the six
staged channels into the ring, no loopback, playback x MASTER, saturation, PCM16 / PCM24 decoding, bad packets); the
PCM16 packing; 20 s of a fast (44,117.6 Hz) and a slow (44,070 Hz) I2S clock against the host's frames with renders
at random times: no glitch, every frame once and in order both ways, the rates matched, the rings' fill well inside
them; the host stopping for 50 ms each way: counted, re-primed, in order again. The settings: `tests/cr_settings_test.c`.

Only the device can show: **enumeration on macOS and Windows** (the two audio devices and the MIDI port appear, the
names, 44.1 kHz accepted; the console with both Off, the replug), long-run **clock drift** against a real host (no
`ua_*_underruns` / `_overruns` over an hour), the 540-byte capture packets on the JieLi controller (Melodee's largest
was the same 540), both streams at once with a full chord (`audio_max_all_us`), and iPadOS.

## Where it lives

| File | What |
| --- | --- |
| `firmware/src/usb.c` | the descriptors (`CFG_DESC` with `usb_audio_desc.h`, `CFG_DESC_CDC`), the strings, EP0 (SET_INTERFACE, the UAC1 sampling-frequency requests), `usb_cdc_on`, `usb_replug`, `ua_off_set` / `ua_off_apply`, `ua_service` |
| `firmware/src/usb_audio.c` | Melodee's endpoint service: `ua_cfg_build`, SET_INTERFACE, the isochronous endpoints in TIMER5 |
| `firmware/src/usb_audio_stream.c` | the rings, the servo, the packet formats, `ua_audio` (no hardware: the host test builds it) |
| `firmware/src/usb_audio_desc.h` | the two audio functions' descriptors |
| `firmware/src/fx.c` | `ua_stage` and the part captures (`mix_part`), `fx_usb_fixed` / `usb_fixed_dac` (USB Level) |
| `firmware/src/choralroot.c` | the master tap (the `mix_block` shim, before the click) |
| `firmware/src/audio.c` | USB Level Fixed's DAC scaling, then `ua_audio` with the IRQs off |
| `firmware/src/main.c` | `ua_service` in TIMER5 (every 250 µs at most, nested in the render too); `ua_off_apply` in the main loop |
| `firmware/src/cr_settings.[ch]`, `cr_ui.c` | the three settings and their Options entries |
| `firmware/src/console.c` | `status` / `dbg`: the `ua_*` counters (since the boot) |

Build flags: `FELUCCA_UAC` (1: the USB audio) and `FELUCCA_CDC` (1: the console, presented as above). `FELUCCA_UAC=0`
gives the previous MIDI + console device (no audio). The update loader is unchanged (byte-identical).
