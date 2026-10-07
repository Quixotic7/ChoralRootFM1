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

Three source files are ports and keep the licence of their originals:
`firmware/src/phys_dsp.c` (DaisySP, MIT), `firmware/src/phys_symp.c` (Rings, MIT), and `firmware/src/cz_native.c`
(MAME uPD933, BSD-3-Clause; through Melodee's CZ-1 engine, below). The FM6 engine
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
| The PHASE engine's waveforms: a C port of the oscillator of CrispyZebra (<https://github.com/hugelton/CrispyZebra>) | GPL-3.0 | `firmware/src/eng_phase.c` |
| The DRUM voices and kit | GPL-3.0-only | `firmware/src/drum_voice.c`, `firmware/src/eng_drum.c` |
| The Hügelton Sample Pack: Felucca's drum sounds, made by `tools/gen_waves.py` (not CC0) | GPL-3.0-only | `tools/gen_waves.py` |
| The panel picture | GPL-3.0-only | `docs/panel.jpg` |
| The Fukiai icon font (<https://github.com/hugelton/Fukiai>): the firmware's icons (rasterised at build time by `tools/gen_aa_icons.py`) and the web editor's | MIT | `web/fukiai.ttf`, `LICENSES/MIT-Fukiai.txt`, `web/FUKIAI-LICENSE.txt` |

## Third-party material

| What | Licence | Where |
| --- | --- | --- |
| Inter Tight font by The Inter Project Authors: the UI text, rasterised into the firmware at build time (`tools/gen_aa_font.py`; the generated tables are not offered as a font, and the font declares no Reserved Font Name) | SIL OFL 1.1 | `assets/fonts/InterTight[wght].ttf`, `LICENSES/OFL-InterTight.txt` (also `assets/fonts/OFL.txt`) |
| Instrument samples (Versilian Studios VSCO-2 Community Edition, VCSL) | CC0 1.0 | `assets/samples-cc0/`, provenance in `ATTRIBUTION.txt` there |
| DaisySP by Electrosmith, Corp and Emilie Gillet (<https://github.com/electro-smith/DaisySP>): the PHYS engine's modal and string models and the resonator, ported to fixed point | MIT | `firmware/src/phys_dsp.c`, `LICENSES/MIT-DaisySP.txt` |
| Rings by Emilie Gillet (<https://github.com/pichenettes/eurorack>): the PHYS engine's sympathetic strings, ported to fixed point | MIT | `firmware/src/phys_symp.c`, `LICENSES/MIT-Rings.txt` |
| Melodee by Kerem Kilic (Ellic Studio), a fork of Felucca (<https://github.com/keremimo/melodee>): ChoralRoot's FM6 engine (Dexed's rendering, 16 voices, DX7 SysEx import / export, the 32-voice bank, Melodee's factory voices F9..F24), the voice model it needs (voice slots, the budget in units, the engine hooks; `core.h`, `voice.c`, `engines.c`'s engine state), the FM6 tables of `tools/gen_tables.py` and its FM6 test; `eng_fm6.c`, `fm6_core.c`, `fm6_bank.c`, `fm6_store.c` and `tests/fm6_test.c` carry "Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)" (kept as they are) | GPL-3.0-only | `firmware/src/eng_fm6.c`, `firmware/src/fm6_core.c`, `firmware/src/eng_fm6_rom.h`, `firmware/src/fm6_bank.c`, `firmware/src/fm6_store.c`, `tests/fm6_test.c` |
| MSFA (Music Synthesizer for Android, Copyright 2012 Google Inc.) and Dexed (Copyright 2013-2025 Pascal Gauthier, <https://github.com/asb2m10/dexed>; portamento rates by Jean Pierre Cimalando), through Melodee: the FM6 engine restates their synthesis in fixed-point C so that it renders the samples Dexed renders (MSFA's envelopes, pitch envelope, LFO, operator kernels and Dx7Note, Apache-2.0; Dexed's MARK I and OPL engines and its voice handling, GPL-3.0-or-later); their DX7 measurement tables and the tables Dexed computes at start are used as data. The FM6 factory patches are Felucca's own (F1..F8) and Melodee's own (F9..F24) | Apache-2.0, GPL-3.0-or-later | `firmware/src/fm6_core.c`, `firmware/src/eng_fm6.c`, `tools/gen_tables.py`, `LICENSES/Apache-2.0-msfa.txt` |
| Melodee by Kerem Kilic (Ellic Studio), its CZ-1 engine (docs/CZ1.md): native Casio CZ-1 tones (the 144-byte tone, its check and panel values, the eight-point envelopes, the 64 factory tones' build tools), the eight 16-tone banks, Casio tone SysEx in / out; `cz_store.c` and `cz_bank.c` carry "Copyright (C) 2026 Kerem Kilic (Ellic Studio)", `eng_cz.c` keeps Leo Kuroshita's and Kerem Kilic's lines of the envelope it took from Melodee's `eng_phase.c` (kept as they are) | GPL-3.0-only | `firmware/src/eng_cz.c`, `firmware/src/cz_patch.h`, `firmware/src/cz_edit.h`, `firmware/src/cz_legacy.h`, `firmware/src/cz_bank.c`, `firmware/src/cz_store.c`, `tools/gen_cz1_factory.py`, `tools/make_cz1_factory.py` |
| Melodee's USB audio (Melodee 0.11.1, <https://github.com/keremimo/melodee>; docs/USB-AUDIO.md): the two UAC1 functions (stereo playback with explicit feedback, the multichannel recording), their endpoint service, the rings and the clock servo, the configuration rebuilt without a function switched off; ChoralRoot re-cut it for six capture channels. `usb_audio.c`, `usb_audio_stream.c` and `usb_audio_desc.h` keep Melodee's header (Leo Kuroshita, Hügelton Instruments) with a line for ChoralRoot's changes | GPL-3.0-only | `firmware/src/usb_audio.c`, `firmware/src/usb_audio_stream.c`, `firmware/src/usb_audio_desc.h`, `firmware/src/usb.c` |
| MAME uPD933 device model by Devin Acker: native CZ phase functions, envelope rate law and logarithmic amplitude adapted to fixed-point C (through Melodee) | BSD-3-Clause | `firmware/src/cz_native.c`, `LICENSES/BSD-3-Clause-uPD933.txt` |
| Casio CZ-1 preset tones A-1..H-8 (Casio Computer Co., Ltd., 1986): the 64 factory sounds as native tone data, combined from the "64 original CZ-1 patches" SysEx (GeoCities CZ-101 page, archived 2009) and Oli Larkin's VirtualCZ conversions of the same presets (LCD names, line levels, velocity). Included as sound data for CZ-1 compatibility; the rights stay with Casio. See `assets/cz1-factory/README.md` | Casio's (no licence granted) | `assets/cz1-factory/cz1-factory.syx`, `tools/make_cz1_factory.py`, `tools/gen_cz1_factory.py` |
| klattsch by Tony Gies (<https://github.com/tgies/klattsch>): design reference for the VOICE engine; no code copied. Formant data from Klatt (1980) / Hillenbrand et al. (1995) | MIT (klattsch) | credit only |
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

No M-VAVE material is part of Felucca. The firmware links no vendor code and contains no vendor
data, and the packages hold only Felucca and the three SDK files above. The installer's
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

## Radio

Felucca never enables the Bluetooth / Wi-Fi radio of the hardware.
