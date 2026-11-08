# Felucca licensing

Felucca is free software, licensed under the GNU General Public License, version 3 only
(`GPL-3.0-only`, full text in `LICENSE`). That covers the code and its own assets. A few
bundled or ported parts keep their own licences; they are listed below, and their licence
texts are in `LICENSES/`.

Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments

## What is GPL-3.0-only

Every file in this tree that carries an `SPDX-License-Identifier: GPL-3.0-only` header:

- the firmware: `firmware/` (app, HAL, update loader)
- the build script and tools: `build.sh`, `tools/`
- the web pages (installer, editor) and their tests: `web/` (not the Fukiai font, below)
- the host tests: `tests/`

One source file is a port and keeps the licence of its original: `firmware/src/cz_native.c` (MAME uPD933,
BSD-3-Clause; through Melodee's CZ-1 engine, below). Two are felucca-bsp's, taken unchanged, and keep MPL-2.0:
`firmware/hal/fm1_flash_wait.h` and `tests/flash_wait_test.c` (the flash driver's busy-safe wait, below). Two more were, until their engine left the tree (docs/HISTORY.md
3y232): `firmware/src/phys_dsp.c` (DaisySP, MIT) and `firmware/src/phys_symp.c` (Rings, MIT), the PHYS engine's, in
ChoralRoot's image up to 0.17, whose packages the site still offers: their licence texts stay in `LICENSES/`. The FM6 engine
(`firmware/src/fm6_core.c`, `firmware/src/eng_fm6.c` and its patches, the bank and the SysEx: Melodee's, below) restates
MSFA (Apache-2.0) and Dexed (GPL-3.0-or-later) in fixed-point C. The firmware built with them is GPL-3.0-only as
a whole.

You may use, study, change and share Felucca under the GPL. If you distribute Felucca, or
firmware derived from it, you must also give your recipients its complete corresponding
source under the same licence. That includes devices that ship with modified Felucca inside.

## Hügelton Instruments' own work

All by Hügelton Instruments (Leo Kuroshita), in this tree:

| What | Licence | Where |
| --- | --- | --- |
| Felucca: firmware, tools, web pages, tests | GPL-3.0-only | `LICENSE` |
| The PHASE engine's waveforms: a C port of the oscillator of CrispyZebra (<https://github.com/hugelton/CrispyZebra>); in ChoralRoot up to 0.17 | GPL-3.0 | `firmware/src/eng_phase.c` (left the tree in 3y232; in the tags) |
| The DRUM voices and kit (never in ChoralRoot's image) | GPL-3.0-only | `firmware/src/drum_voice.c`, `firmware/src/eng_drum.c` (left the tree in 3y232; in the tags) |
| The Hügelton Sample Pack: Felucca's drum sounds, made by `tools/gen_waves.py` (not CC0) | GPL-3.0-only | `tools/gen_waves.py` |
| The panel picture | GPL-3.0-only | `docs/panel.jpg` |
| The Fukiai icon font (<https://github.com/hugelton/Fukiai>): the firmware's icons (rasterised at build time by `tools/gen_aa_icons.py`) and the web editor's | MIT | `web/fukiai.ttf`, `LICENSES/MIT-Fukiai.txt`, `web/FUKIAI-LICENSE.txt` |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Inter Tight font by The Inter Project Authors: the UI text, rasterised into the firmware at build time (`tools/gen_aa_font.py`; the generated tables are not offered as a font, and the font declares no Reserved Font Name) | SIL OFL 1.1 | `assets/fonts/InterTight[wght].ttf`, `LICENSES/OFL-InterTight.txt` (also `assets/fonts/OFL.txt`) |
| Instrument samples (Versilian Studios VSCO-2 Community Edition, VCSL) | CC0 1.0 | `assets/samples-cc0/`, provenance in `ATTRIBUTION.txt` there |
| DaisySP by Electrosmith, Corp and Emilie Gillet (<https://github.com/electro-smith/DaisySP>): the PHYS engine's modal and string models and the resonator, ported to fixed point; in ChoralRoot's image in 0.16 and 0.17 (the site still offers them) | MIT | `firmware/src/phys_dsp.c` (left the tree in 3y232; in the tags), `LICENSES/MIT-DaisySP.txt` |
| Rings by Emilie Gillet (<https://github.com/pichenettes/eurorack>): the PHYS engine's sympathetic strings, ported to fixed point; in ChoralRoot's image in 0.16 and 0.17 (the site still offers them) | MIT | `firmware/src/phys_symp.c` (left the tree in 3y232; in the tags), `LICENSES/MIT-Rings.txt` |
| Melodee by Kerem Kilic (Ellic Studio), a fork of Felucca (<https://github.com/keremimo/melodee>): ChoralRoot's FM6 engine (Dexed's rendering, 16 voices, DX7 SysEx import / export, the 32-voice bank, Melodee's factory voices F9..F24), the voice model it needs (voice slots, the budget in units, the engine hooks; `core.h`, `voice.c`, `engines.c`'s engine state), the FM6 tables of `tools/gen_tables.py` and its FM6 test; `eng_fm6.c`, `fm6_core.c`, `fm6_bank.c`, `fm6_store.c` and `tests/fm6_test.c` carry "Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)" (kept as they are) | GPL-3.0-only | `firmware/src/eng_fm6.c`, `firmware/src/fm6_core.c`, `firmware/src/eng_fm6_rom.h`, `firmware/src/fm6_bank.c`, `firmware/src/fm6_store.c`, `tests/fm6_test.c` |
| MSFA (Music Synthesizer for Android, Copyright 2012 Google Inc.) and Dexed (Copyright 2013-2025 Pascal Gauthier, <https://github.com/asb2m10/dexed>; portamento rates by Jean Pierre Cimalando), through Melodee: the FM6 engine restates their synthesis in fixed-point C so that it renders the samples Dexed renders (MSFA's envelopes, pitch envelope, LFO, operator kernels and Dx7Note, Apache-2.0; Dexed's MARK I and OPL engines and its voice handling, GPL-3.0-or-later); their DX7 measurement tables and the tables Dexed computes at start are used as data. The FM6 factory patches are Felucca's own (F1..F8) and Melodee's own (F9..F24) | Apache-2.0, GPL-3.0-or-later | `firmware/src/fm6_core.c`, `firmware/src/eng_fm6.c`, `tools/gen_tables.py`, `LICENSES/Apache-2.0-msfa.txt` |
| Melodee by Kerem Kilic (Ellic Studio), its CZ-1 engine (docs/CZ1.md): native Casio CZ-1 tones (the 144-byte tone, its check and panel values, the eight-point envelopes, the 64 factory tones' build tools), the eight 16-tone banks, Casio tone SysEx in / out; `cz_store.c` and `cz_bank.c` carry "Copyright (C) 2026 Kerem Kilic (Ellic Studio)", `eng_cz.c` keeps Leo Kuroshita's and Kerem Kilic's lines of the envelope it took from Melodee's `eng_phase.c` (kept as they are) | GPL-3.0-only | `firmware/src/eng_cz.c`, `firmware/src/cz_patch.h`, `firmware/src/cz_edit.h`, `firmware/src/cz_legacy.h`, `firmware/src/cz_bank.c`, `firmware/src/cz_store.c`, `tools/gen_cz1_factory.py`, `tools/make_cz1_factory.py` |
| Melodee's USB audio (Melodee 0.11.1, <https://github.com/keremimo/melodee>; docs/USB-AUDIO.md): its UAC1 multichannel recording function, its endpoint service, the capture ring and the clock servo, the configuration rebuilt without the function when switched off; ChoralRoot re-cut it for six capture channels and left out Melodee's stereo playback function. `usb_audio.c`, `usb_audio_stream.c` and `usb_audio_desc.h` keep Melodee's header (Leo Kuroshita, Hügelton Instruments) with a line for ChoralRoot's changes | GPL-3.0-only | `firmware/src/usb_audio.c`, `firmware/src/usb_audio_stream.c`, `firmware/src/usb_audio_desc.h`, `firmware/src/usb.c` |
| SLOOP 2.5 (<https://github.com/isod89/sloop-fm1>, a fork of Felucca), after Felucca 1.1.5 by Leo Kuroshita (Hügelton Instruments, <https://github.com/hugelton/Felucca>): USB audio at 48 kHz (docs/USB-AUDIO.md "48 kHz"): the two discrete rates in one alternate setting and the host's `SET_CUR`, the 44.1 -> 48 kHz polyphase resampler (160 / 147) and SLOOP's 12-tap Q15 table with its generator (Felucca's is 24 taps in Q30), the sine-fit test; ChoralRoot re-cut the resampler for six channels and its own capture ring. `tools/gen_uac_fir.py` keeps Leo Kuroshita's line with SLOOP's and ChoralRoot's | GPL-3.0-only | `firmware/src/uac_fir.h`, `tools/gen_uac_fir.py`, `firmware/src/usb_audio_stream.c` (`ua_resample`, `ua_rate_set`), `firmware/src/usb_audio.c`, `firmware/src/usb_audio_desc.h`, `tests/cr_usbaudio48_test.c` |
| Velo (fm1-velocity, <https://github.com/charlesvestal/fm1-velocity>) and MONK-1 (fm1-monk, <https://github.com/charlesvestal/fm1-monk>) by Charles Vestal, both built on Felucca's platform layer: velocity and aftertouch from the FM-1's own keys (docs/VELOCITY.md): the key sensing (`cr_sense.c`, from Velo's `sense.c`: the state machine on the pulled-up and driven samples, the log-scale velocity, MONK-1's rest-following pressure, restated in integers), the scan's analog sampling and the driven samples (`hal/fm1_input.h`), the converter's non-blocking reads (`hal/fm1_adc.h`), MONK-1's Touch: Velocity law (`mod.c cr_touch_gain`); the measurements in Velo's `notes/01-lab.md`. `fm1_input.h` and `fm1_adc.h` keep Leo Kuroshita's line with a line for Velo's and ChoralRoot's | GPL-3.0-only | `firmware/src/cr_sense.c`, `firmware/hal/fm1_input.h`, `firmware/hal/fm1_adc.h`, `firmware/src/mod.c` |
| felucca-bsp's busy-safe flash wait by Julius Vitkauskas (felucca-bsp #12, as merged in 2d37ec8, <https://github.com/hugelton/felucca-bsp>): an erase or a page program never returns to XIP while the flash part is busy; past 2 s / 50 ms the part and the chip are reset. `fm1_flash_wait.h` and its host model are taken unchanged (but for a provenance line and the include path) and keep their licence; `hal/fm1_flash.h` (GPL-3.0-only) carries the reset routine and the two call sites with a copyright line for Julius Vitkauskas. MPL-2.0 is compatible with the GPL as a larger work (MPL-2.0 section 3.3; these files are not marked "Incompatible With Secondary Licenses") | MPL-2.0 | `firmware/hal/fm1_flash_wait.h`, `tests/flash_wait_test.c`, `LICENSES/MPL-2.0.txt` |
| MAME uPD933 device model by Devin Acker: native CZ phase functions, envelope rate law and logarithmic amplitude adapted to fixed-point C (through Melodee) | BSD-3-Clause | `firmware/src/cz_native.c`, `LICENSES/BSD-3-Clause-uPD933.txt` |
| Casio CZ-1 preset tones A-1..H-8 (Casio Computer Co., Ltd., 1986): the 64 factory sounds as native tone data, combined from the "64 original CZ-1 patches" SysEx (GeoCities CZ-101 page, archived 2009) and Oli Larkin's VirtualCZ conversions of the same presets (LCD names, line levels, velocity). Included as sound data for CZ-1 compatibility; the rights stay with Casio. See `assets/cz1-factory/README.md` | Casio's (no licence granted) | `assets/cz1-factory/cz1-factory.syx`, `tools/make_cz1_factory.py`, `tools/gen_cz1_factory.py` |
| klattsch by Tony Gies (<https://github.com/tgies/klattsch>): design reference for the VOICE engine (up to 0.17; its source left the tree in 3y232); no code copied. Formant data from Klatt (1980) / Hillenbrand et al. (1995) | MIT (klattsch) | credit only |
| Melodee by Kerem Kilic (Ellic Studio), its Prophet-5 engine (docs/PROPHET.md; <https://github.com/keremimo/melodee>): the native program (the 133 raw bytes, their indices, INIT, SysEx decode / encode), the two band-limited oscillators with hard sync and Poly-Mod, the four-pole ladder in its two characters, the envelopes, the wheel's source, the voices and their budget units (Melodee: five of three units; here four of four), taken at Melodee commit 8a3074b ("fix(prophet): Lo Freq B seven octaves, free-running phase, smoother saturation": its last integer version, on b351a14 and 1fbeccd), with the part's DC blocker of 7cd66fb and the factory bank's packed form and generator of 1bc4555 (Melodee 1.2.1); the engine's tables in `tools/gen_tables.py` (`p5_tables`: 8a3074b's expressions); the panel's names and ranges of its `prophet_edit.h` for ChoralRoot's editor pages. The files carry Leo Kuroshita's line (Felucca's, as Melodee's sources do by their SPDX header alone: added here with Kerem Kilic's "Modifications" line, as on `eng_cz.c`) | GPL-3.0-only | `firmware/src/eng_prophet_test.c`, `firmware/src/prophet_patch.c`, `tools/gen_prophet_factory.py`, `tools/gen_tables.py` |
| Rosen's remix of ChoralRoot 0.14-dev ("Choralroot-remix-step20-timing", sent to the project in October 2026; a fork of ChoralRoot, GPL-3.0-only as it is): the adaptation of Melodee's Prophet-5 engine to ChoralRoot's engine API that showed it fits (the state in the parts' engine state, engine 16 appended after FM TONE, native pages through `eng_deep_t`: its `firmware/src/eng_prophet_test.c`, `README-NEW-FEATURES.md`), and its stage 36's three optimisations of the sample loop, taken as they are and proven bit-exact here (a silent oscillator skipped, the envelope's terms once a sample, hard sync's 32-bit phase products: its `docs/STAGE36-PROPHET-OPTIMIZATION.md`, `STAGE36-README.txt`). Its page plan (OSC A, OSC B, OSC B+, MIXER, FILTER, FLT ENV, AMP ENV, LFO, POLYMOD) was the start of ChoralRoot's 19 pages; its half-buffer, dual-core, voice-cap and user-slot changes are not taken | GPL-3.0-only | `firmware/src/eng_prophet_test.c` (`P5_STAGE36`) |
| Sequential Prophet-5 / 10 factory programs v1.03 (Sequential, 2025; <https://sequential.com/support/download/prophet-5-10-sounds/>): the 200 programs of `P5_Factory_Programs_USER_v1.03.syx`, unchanged native data; the ones `assets/prophet5-factory/shipped.txt` names are compiled in as the factory presets of the P5 engine (through Melodee, whose asset and README they are). Included as sound data for Prophet-5 compatibility; the rights stay with Sequential. In the build only with `FELUCCA_P5_FACTORY` 1. A release carries exactly the 115 that `shipped.txt` lists (the owner's decision, 2026-10-13: his favourites), not the other 85, which the site does not offer either (docs/PROPHET.md "Open decisions" 2). See `assets/prophet5-factory/README.md` | Sequential's (no licence granted) | `assets/prophet5-factory/prophet5-v1.03.syx`, `tools/gen_prophet_factory.py` |
| fm1-x0x (GPL-3.0), after Melodee's dual-core audio by Kerem Kilic (GPL-3.0, <https://github.com/keremimo/melodee>), following JieLi's AC79 SDK `EnableOtherCpu` (Apache-2.0, <https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK>; the start / hold sequence and registers, restated, no SDK code copied): the second core's bring-up, its entry and its waiting loop, the PC-limit opening (docs/DUALCORE.md) | GPL-3.0, Apache-2.0 | `firmware/hal/fm1_cpu1.h`, `firmware/hal/fm1_cpu1.S`, `firmware/hal/fm1_guard.h` (`fm1_guard_pc_open`), `LICENSES/Apache-2.0.txt` |
| SLOOP 2.6 (<https://github.com/isod89/sloop-fm1>, a fork of Felucca; its sources carry Leo Kuroshita's, Hügelton Instruments' copyright line): the design of the Clean output chain's look-ahead limiter: the output one audio block late and the limiter's gain a straight line across each block to what the loudest sample of the block going out and of the next one needs (its `firmware/src/fx.c` `lim_block`). The design only, written anew; no code copied. The hold, the release, the peak kept per block and the arithmetic are ChoralRoot's (docs/AUDIO-QUALITY.md "The look-ahead") | GPL-3.0-only | `firmware/src/fx.c` (`clean_block`) |
| SLOOP 2.6 (as above), its stereo chorus: two taps on the one chorus line, returned left and right, the LFO taken once a block with the read points a straight line across it (its `firmware/src/fx.c` `fx_buses`). The design only, written anew; no code copied. SLOOP's taps are half a turn of the LFO apart; the quarter turn, the width parameter, the line that ends on the next block's start and the idle path are ChoralRoot's (docs/FX.md "The chorus, stereo") | GPL-3.0-only | `firmware/src/cr_fx.c` (`xc_block`) |
| Felucca 1.1.5 by Leo Kuroshita (Hügelton Instruments, <https://github.com/hugelton/Felucca>, commit 276f72a, its issue #137): MASTER read before the audio starts and followed under the splash (its `firmware/src/master.c`), ported after ChoralRoot's fork point; `cr_out_init`'s part is ChoralRoot's (docs/AUDIO-QUALITY.md "MASTER at power-on") | GPL-3.0-only | `firmware/src/master.c`, `firmware/src/main.c` |
| Melodee PR 21 "fix(dual-core): raise the core supply and survive CPU1 misreads, faults and stalls" by Kerem Kilic / Melodee (GPL-3.0, <https://github.com/keremimo/melodee>): the core supply rails (P33 analog levels after JieLi's AC79 SDK p33.h, Apache-2.0, restated), the second core's job validation (sequence + check word), the fallback to one core, the fault path that holds core 1 and resumes core 0, the fault-injection tool (docs/DUALCORE.md "Reliability") | GPL-3.0-only | `firmware/hal/fm1_power.h`, `firmware/hal/fm1_cpu1.h`, `firmware/hal/fm1_vec.S`, `firmware/src/cr_cpu1.h`, `tools/core1_fault_test.py` |
| Optimist (<https://github.com/w0ts/optimist>, commit 498a3ae47c4f01a6c0cc891c794f8cd2ed78cd66), through Melodee by Kerem Kilic (Ellic Studio) at Melodee nightly 3fe570d (<https://github.com/keremimo/melodee>): the bare-metal Bluetooth LE stack (link layer, L2CAP, ATT / GATT, SMP pairing and bonding, AES-CCM, BLE MIDI framing, the stock calibration reader), the WL82 baseband driver and the radio's register access, and their host tests and fake radio; with Melodee's changes (the initial read, bounded security settling, exact CCC acknowledgement, POOL placement, the iterative radio replay). Files kept as they are, notices kept; ChoralRoot's changes: the driver's interrupt attach (`ble_hw_wl82.c`), the program's section macro (`ble_rf_tables.h`). Provenance: `docs/ble/BLE_MIDI.md` | GPL-3.0-only | `firmware/src/ble/`, `firmware/hal/fm1_ble.h`, `firmware/hal/fm1_ble_rf.h`, `tests/ble_*`, `tests/ble_fake/`, `tests/fixtures/ble_rf_exec_reference.h`, `docs/ble/` |
| Melodee's Bluetooth MIDI glue (`firmware/src/midi_ble.c` at 3fe570d, Kerem Kilic): the rings each way, clock ahead of notes, the bond record, the radio's start and the calibration's choice; ChoralRoot's `cr_ble.c` / `cr_ble.h` adapt its peripheral half and carry its line | GPL-3.0-only | `firmware/src/cr_ble.c`, `firmware/src/cr_ble.h` |
| Melodee's USB MIDI host by Kerem Kilic (Ellic Studio), with the host enumeration, input and descriptor parsing contributed by recursivefieldinstruments (Melodee nightly 3fe570d, <https://github.com/keremimo/melodee>): the USB-C port as host of a USB-MIDI device, alone or behind a hub; the register sequences after the AC79 SDK's USB0 host code (restated); its host test with the SIE and device model. `usb_host.c` keeps its header with a line for ChoralRoot's changes, `usb_host_desc.h` is unchanged, `fm1_usb.h` gained its two host functions | GPL-3.0-only | `firmware/src/usb_host.c`, `firmware/src/usb_host_desc.h`, `firmware/hal/fm1_usb.h`, `tests/usb_host_test.c` |
| JieLi AC79 SDK's radio calibration (`wl_rf_common.a`, Copyright (c) 2024-2026 Zhuhai Jieli Technology Co., Ltd.), rewritten in C by Charles Vestal for felucca-bsp (through Optimist and Melodee): the VCO bank scan and the RX filter, TX LO and RX DC trims, run when no stored calibration exists; the file's header states the changes (Apache-2.0 section 4(b)) | Apache-2.0 | `firmware/hal/fm1_rf_cal.h`, `LICENSES/Apache-2.0.txt` |
| The radio's start-up program (`ble_rf_tables.h`, from Optimist's `ble_rf_tables_v15.h` through Melodee): register and table values captured from the stock M-VAVE V15 firmware's radio start-up. Data, not code anyone here wrote; Optimist's licensing record states no upstream licence for it, and the file's GPL identifier does not relicense it (the provenance note of `docs/ble/BLE_MIDI.md`, kept). Shipped in the package's resource region (flash 0x93000.., `.res.ble`), not in the app (the owner's decision, docs/BLE-LINK.md decision 5) | none stated (M-VAVE V15-derived data) | `firmware/hal/ble_rf_tables.h` |
| Melodee 1.4 by Kerem Kilic (Ellic Studio) (GPL-3.0-only, <https://github.com/keremimo/melodee>), commits 6da744c "perf(audio): optimize DSP and share voice budget" and cb118c9 "shared sends and lean reverb", through a survey of them: the ideas of HISTORY 3y50, an effect run on the second core while the first runs the others (its `audio_batch` jobs), the Prophet-5 part's coefficients worked out once and reused while the program does not change (its `p5_cache_update`), and word-at-a-time memset / memcpy. Design ideas only, written anew for ChoralRoot's integer code and job protocol; no Melodee 1.4 code was copied (its DSP is float) | GPL-3.0-only | `firmware/src/fx.c` (`mix_fx_split`, `c1_fx_job`), `firmware/src/cr_fx.c` (`xf_prep`, `xf_delay`, `xf_reverb`), `firmware/src/eng_prophet_test.c` (`p5_coef_t`, `p5_coef_update`), `firmware/src/libc.c` |
| The SMK-37 board layer by genkigenki (M. Geisler, genkigenki / makkuro), their SMK-37 port of Felucca (<https://github.com/genkigenki/Felucca>, GPL-3.0-only) brought onto ChoralRoot by them in PR #14 (<https://github.com/Quixotic7/ChoralRootFM1/pull/14>, commit 187b2e5, 2026-10-13) and carried since (SMK37.md): the M-Vave SMK-37 build target behind `BOARD_SMK37` (the input HAL with the keys' velocity, the pads, knobs, faders, wheels and button LEDs; the LCD pins and the pad LEDs; the SMK flash map and its guards; the store's owner tag; the boot stub's and the update loader's SMK places; the per-model identities; the package's region table; the installers' SMK support and their tests), their fix for the stock firmware's key mapping (0.24-beta3-smk37.1), and the first version of SMK37.md. The files carry "SMK-37 port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)" | GPL-3.0-only | `firmware/hal/smk_input.h`, `firmware/hal/fm1_flash_map.h`, `firmware/hal/fm1_lcd_hw.h`, `BOARD_SMK37` blocks in `firmware/`, `tools/build.py`, `tools/fm1pkg_make.py`, `tools/fm1_install.py`, `web/fm1ota.js`, `web/fm1pkg.js`, `web/smk37.html`, `tests/smk_*`, `tools/emu/emu_smk_hal.h`, `SMK37.md` |
| fm1-to-smk37 by genkigenki (<https://github.com/genkigenki/fm1-to-smk37>): the SMK-37 hardware notes (pins, matrix, UART, flash map, update) the SMK-37 build and its TRS MIDI out were checked against; no text of it is in this tree | CC BY-NC 4.0 | credit only |
| Vector37 by genkigenki (<https://github.com/genkigenki/Vector37>), their SMK-37 Pro simulator: the arrangement of the SMK-37 emulator's panel; no code copied | GPL-3.0 | `tools/emu/web/smk/` (credit only) |
| Skiff by illuminus5 (<https://github.com/illuminus5/skiff>, a fork of Felucca), its SMK-37 TRS MIDI OUT driver (`firmware/hal/smk_uart_tx.h`, Skiff 1.4 and 2.2, commit 76b5f9c: UART1 TX on PC7 at 31250 baud, the byte ring and its 10 kHz tick, the realtime slot, the timeout), brought to ChoralRoot by finlaysteven1-netizen (GitHub issue 42: the header as Skiff has it, the USB-MIDI packet to bytes, the hooks in `main.c` and `usb.c`), modified 2026-11-06 (the packet's bytes in the header, the releases that always get in, Options > TRS MIDI Out). The register sequence is from genkigenki's SMK-37 hardware notes (fm1-to-smk37 docs/01-hardware.md, credit only) | GPL-3.0-only | `firmware/hal/smk_uart_tx.h`, `firmware/src/usb.c`, `firmware/src/main.c` |
| JieLi AC79 SDK by JieLi Technology: three of its files go into every `.fwsc` package (below); they are vendored in `tools/sdk/` with the SDK's licence | Apache-2.0 | `tools/sdk/LICENSE`, `LICENSES/Apache-2.0.txt` |

On the device, HOME held > ABOUT opens the information screen; turning PRESETS scrolls on into
CREDITS, a short list of these authors, licences and source URLs.

## JieLi SDK files in the packages (Apache-2.0)

A `.fwsc` package made by `tools/build.py` (with `tools/fm1pkg_make.py`; this is the package the
web installer installs) holds three unmodified files from the JieLi AC79 SDK
(<https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK>, `cpu/wl82/tools/`). They are read from your SDK
checkout at build time when one is present, else from `tools/sdk/` where the three files are vendored, unmodified,
with the SDK's `LICENSE` (see BUILDING.md and `tools/sdk/README.md`).

| File in the package | What it is |
| --- | --- |
| `uboot.boot` | the first-stage boot loader (SPL) |
| `cfg_tool.bin` | the chip configuration block |
| `eq_cfg_hw.bin` | the default hardware EQ table |

They are licensed under the Apache License, Version 2.0 (`LICENSES/Apache-2.0.txt`), not under
the GPL, and their copyright stays with JieLi Technology. The SDK has no NOTICE file. Apache-2.0
files may be distributed together with GPL-3.0 code; Felucca itself stays GPL-3.0-only.

Every distribution of a package carries the licence texts with it: the release folder
(`tools/build.py --release`) and the site (`web/make_site.py`, next to the package in
`firmware/` and linked from the installer page).

## No vendor material

No M-VAVE code is part of ChoralRoot, and the app links no vendor code. One piece of vendor-derived data is shipped
since 3y236: the radio's start-up program for Bluetooth MIDI (above: register values captured from the stock V15
firmware, as Optimist and Melodee ship them), in the package's resource region; the FM-1's packages hold it, the
SMK-37's do not. Otherwise the packages hold only ChoralRoot and the three SDK files above. The installer's
"Return to official V15" holds only the official file's size and SHA-256: you select the
official firmware file you downloaded yourself, and it is checked and installed in your browser,
never uploaded or redistributed.

## Contributions

Contributions are welcome under GPL-3.0-only.

## Trademarks

"Felucca" and "Hügelton Instruments" are names of Hügelton Instruments.

"M-VAVE" and "FM-1" are trademarks of their respective owners. Felucca is independent
firmware that runs on FM-1 hardware. It is not affiliated with, endorsed by or supported
by those owners.

"Sequential" and "Prophet" are trademarks of their owner; "Casio" and "CZ" of theirs. The P5 (in the code:
PROPHET) and CZ-1 engines are independent software models named after the instruments whose patch data they read;
they are not affiliated with, endorsed by or supported by those owners.

## Radio

ChoralRoot enables the 2.4 GHz radio only for Bluetooth LE MIDI on the FM-1, and only when Options > Bluetooth is
On (Off by default; docs/MANUAL.md "Bluetooth MIDI"). Wi-Fi is never enabled. The app does not link the SDK's radio
library: the stack and driver are Optimist's (above), the start-up program is the captured data above, and with no
stored calibration the radio is calibrated by `firmware/hal/fm1_rf_cal.h` (Apache-2.0, above).
