# CZ-1 factory tones

`cz1-factory.syx` holds Casio's 64 CZ-1 preset tones (A-1 BRASS 1 to H-8 TYPHOON SOUND) as native
CZ-1 tone dumps: 64 messages `F0 44 00 00 70 21 60` + 288 nibbles (144 bytes, low nibble first) + `F7`,
the format the web editor exports. `tools/gen_cz1_factory.py` turns it into `build/gen/melodee_cz1.h`
at build time: the default BANK A–D and the CZ-1 PRESETS 1–64.

The tones are Casio's (CZ-1, Casio Computer Co., Ltd., 1986). They are included as sound data for
compatibility with the instrument; no licence is granted for them by this project.

## Where they come from

No single source has the complete CZ-1 tones, so `tools/make_cz1_factory.py` combines two:

1. `cz1org64.zip`, "64 original CZ-1 patches for CZ-101, 1000, 3000, 5000" from the old GeoCities
   CZ-101 page (`geocities.com/SunsetStrip/Frontrow/4877/cz101.html`, Wayback Machine capture of
   2009-10-27). Four `.SYX` banks of 16: Casio's 128 synthesis bytes, without the CZ-1's names, line
   levels and velocity amounts.
2. Oli Larkin's `VirtualCZ_Casio_Presets.zip` (<https://olilarkin.co.uk/downloads/VirtualCZ_Casio_Presets.zip>),
   `VST2/CZ1/*.fxp`: the same 64 presets as VirtualCZ parameters, which keep the CZ-1 line LEVEL and the
   pitch / DCW / DCA velocity amounts.

The 128 bytes are kept verbatim, apart from what a CZ-1 tone adds: the CZ-1 DCW key-follow byte (as the
device's own 128-byte import does), the LEVEL and velocity nibbles from VirtualCZ and the 16-character
LCD name (centred, as the CZ-1 shows it). The script checks that every envelope rate, level and sustain
point of both sources agrees before writing the file.

    tools/make_cz1_factory.py CZ1ORG64_DIR VIRTUALCZ_DIR/VST2/CZ1
