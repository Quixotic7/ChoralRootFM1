# FM-1 Transporter review shim

What: local stand-ins for `fm1_ota.require_reviewed` and `fm1fw.Firmware`, the two fm-1-research-lab modules that FM-1-transporter's `tools/fm1t.py write` imports from `$FM1_RESEARCH/tools`.
Why: that repo is private; this lets the Transporter recover a dark FM-1 with ChoralRoot's own review policy.

    FM1_RESEARCH=$PWD/tools/transporter python3 ../FM-1-transporter/tools/fm1t.py write --package build/choralroot.fwsc --ref backup.bin

Policy: accept (1) the official V15 (identity FM-1_015, flash.bin 0x4000..0x92FFF sha256 as MvaveFM1Unbricker; the file sha256 is printed), or (2) a ChoralRoot package (FM-1_920 + `FELUCCA-LOADER-1` marker; head 0x0000..0x3FFF compared with the V15 package at `$FM1_V15` when set, as a printed note only (the head is never written)). Everything else, bad UFW CRCs, no flash.bin, or a flash.bin not 0x93000 bytes, exits.
The flash image is flash.bin (UFW type-0 entry, parsed and CRC-checked as firmware/src/ota.c ota_ufw; 0x400 into the logical image), 0x93000 bytes.
Safety boundary: the Transporter firmware itself only writes 0x4000..0x93000; fm1t.py also refuses packages that change sectors below 0x4000 versus `--ref`.
