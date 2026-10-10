# Recovery of the dark FM-1, 2026-10-09

The flash dumps read through the FM-1 Transporter before the official V15 was written back (`docs/TRANSPORTER-HANDOFF.md`,
"Result"; the analysis in `docs/BOOT-SAFETY.md`, "What the dump showed"). The `.bin` files stay on this Mac only (they
hold the unit's settings and user sounds); this note records what they are.

| file | bytes | sha256 | crc32 |
| --- | --- | --- | --- |
| `backup1.bin` | 1048576 | `6c075920bdca1cf56f13094cd343244bb8ab98d1df6de5e45635b48814198258` | ACCDD26C |
| `backup2.bin` | 1048576 | identical to backup1.bin | ACCDD26C |

State of the flash in them: head 0x0000..0x3FFF = V15; 0x04000..0x24FFF = the package of commit 23a8a75 (sha256
e59c47ee…); 0x25000..0x92FFF = the previous firmware; the staged loader at 0xE0000 and the record at 0xE4F00 valid.
Written afterwards: official V15 (`../MVaveOfficial/V15-FM-1.fwsc`, region hash 6edf3c37…) over 0x04000..0x92FFF,
then ChoralRoot 0.14 (`build/choralroot.fwsc`, file sha256 7a15bfb7…) through `tools/fm1_install.py`.
