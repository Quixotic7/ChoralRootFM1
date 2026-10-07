# Handoff: recover the dark FM-1 with the FM-1 Transporter, from the Mac (2026-10-07)

Paste everything below the line into a Claude session on the Mac.

---

Help me recover my M-VAVE FM-1 (JieLi WL82 / AC791N) with the FM-1 Transporter
(https://github.com/kurogedelic/FM-1-transporter). I am the owner; the firmware is my own GPL-3.0 project,
ChoralRoot FM-1 (https://github.com/Quixotic7/ChoralRootFM1). This continues `docs/UNBRICK-HANDOFF.md`: the Windows
session ran every software step and none reached the device.

**State of the unit.** `tools/fm1_install.py build/choralroot.fwsc --yes` stopped on the Mac with "the loader was
disconnected after 274 requests" (of about 1167). Since then: black screen, and no USB device of any kind.

**What the Windows session established (2026-10-07, Windows 11).**

- Checked after every step, three ways: `Get-PnpDevice` for VID_4C4A / VID_1209 (any state, so remembered devices
  too), `Win32_DiskDrive`, the mido MIDI port list, plus a diff of all present PnP devices against a snapshot and the
  last-arrival date of every USB device. Result every time: nothing. No `WL82 UBOOT1.00`, no `ota-FM-1`, no
  "Unknown USB Device", no arrival event. Windows has never seen a 4C4A or 1209 device on that PC.
- Tried, in order: OCT- and OCT+ held at power-on; one hour on a wall charger; two full minutes switched on and
  plugged in; three quick restarts with the power switch (on 10 s, off, on 10 s, off, on).
- The port is good: a DualSense controller on it enumerated at once, on a root-hub port. Not confirmed: that the
  FM-1 was on that same cable. Try the FM-1 once on a second, known data cable before anything else.
- `py fm1_unbrick.py setup` ran (jl-uboot-tool at `adb3f18`, wl82 loader sha256 `d41da612...`); `find` exits 1,
  "No WL82 UBOOT1.00 device found". Nothing was written to the FM-1 from Windows.
- The package on the PC, `build/choralroot.fwsc`, has sha256 `e59c47ee09bdfc7282394ceaa4ed394dd92e5f299dd086a388fd12ec191ec364`
  (the 0.14-dev build).

**What the code says about the failure** (read, not measured):

- 274 requests is about 34 of 143 app sectors (eight 512-byte reads per 4 KiB sector, `firmware/loader/ldr_core.c`
  `ldr_session`), so the app area is new up to about 0x25000 and old above it. It cannot run.
- Every error path in `ldr_session` returns to `ldr_main` with the loader still alive as `ota-FM-1`. A stalled host
  would not make the port vanish, so the chip reset or lost USB: a battery brown-out (the owner sees the battery
  drain while switched on, even on USB), the loader's 8 s watchdog (`fm1_wdt_arm(0x0D)`, fed only by `ldr_poll`), a
  USB drop on the host side, or a loader crash. Unknown which.
- Why it stayed dead: `ldr_cstart` clears the RAM update record first (`ldr_record_clear`), so after any warm reset
  the SPL boots the half-written app. The boot guard, the watchdog and the OCT update mode all live in that app.
  Resume after a power-on depends on the real SPL honouring the flash record at 0xE4F00; that is tested only against
  an emulated NOR (`tests/ldr_test.c`, `tests/ota_test.c`). This unit says it does not work on hardware, or the record
  or the loader area is not what we expect. The dump decides.
- Felucca issue #61 (https://github.com/hugelton/Felucca/issues/61) is a different case (a complete write, a crash
  at boot, UBOOT reached by itself); ChoralRoot already has its fix (`firmware/hal/fm1_irq.h`, div0 trap off).

**Parts (ordered, arriving about 2026-10-09).**

- Seeed Studio XIAO RP2040, pre-soldered headers (not the RP2350: the Transporter is RP2040 only).
- A USB-C male to 4-pin Dupont cable, four wires: red V+, white D-, green D+, black GND.
- A normal USB-C data cable from the XIAO to the Mac.

**Wiring. Three wires, VBUS not connected** (the FM-1 runs on its battery: charge it fully first).

| XIAO RP2040 | FM-1 USB | wire |
|---|---|---|
| D6 (GP0) | D+ | green |
| D7 (GP1) | D- | white |
| GND | GND | black |
| nothing | VBUS | red: insulate it |

D6 and D7 are the two pins farthest from the XIAO's USB-C port, one on each edge; GND is the second pin from the
USB-C end on the 5V side. Slide the three crimped pins out of the 4-pin housing to reach them. Check the colours
against the plug with a multimeter first; swapped D+ / D- damages nothing but nothing is detected.

**Do, in order, and tell me what each command printed, not a summary.**

1. Read the Transporter's README.md, docs/ARCHITECTURE.md and docs/PROTOCOL.md; they are the authority. Then
   `python3 tools/fm1t.py --help` and read how `write` reviews a package: ARCHITECTURE.md says it uses
   `fm1_ota.require_reviewed` from "fm-1-research-lab". Find out what that needs, and whether it accepts only the
   official package, before the device is connected.
2. Build the XIAO firmware (can be done before the parts arrive): pico-sdk 2.2.0, an ARM GCC toolchain, CMake,
   picotool; `wl82loader.bin` from jl-uboot-tool (https://github.com/kagaimiq/jl-uboot-tool, the commit
   MvaveFM1Unbricker pins is `adb3f18`).
   `git submodule update --init`, then
   `cmake -S . -B build -DPICO_SDK_PATH=$HOME/pico-sdk -DPICO_BOARD=seeed_xiao_rp2040 -DFM1T_LOADER_BIN=/path/to/wl82loader.bin`,
   `make -C build -j8`, `picotool load -f -x build/fm1_transporter.uf2` (or copy the .uf2 to the XIAO's boot disk).
3. Get the official V15 `FM-1.fwsc` from https://www.m-vave.com/download. Check it:
   `python3 fm1_unbrick.py extract FM-1.fwsc app_v15.bin --verify-v15` (MvaveFM1Unbricker; works on macOS) must print
   identity `FM-1_015` and sha256 `6edf3c37fb5bbbc33607c89375ee024d5477c17914d72221c8c68e58a8255686`.
4. FM-1 switched **off**. Wire it to the XIAO, XIAO to the Mac. Then switch the FM-1 **on**: the XIAO sends the
   USB_KEY at power-on, so the order matters. `python3 tools/fm1t.py info`: expect chip key 0x980F, flash ID
   0x856014. If it is not detected: swap D+ / D-, shorten the wires, check the charge, read the console (CDC 0), try
   `rekey`.
5. `python3 tools/fm1t.py dump backup.bin`, twice into two files, and compare their sha256. **Keep the dump and
   write nothing until it is analysed.**
6. Analyse the dump against `build/choralroot.fwsc` and the V15 package, and report:
   - 0x0000..0x3FFF: identical to the V15 package's head?
   - 0x4000..0x92FFF, per 4 KiB sector: equal to the new package, to the old firmware, erased, or mixed; the last
     sector written and whether one sector is torn.
   - 0xE0000..0xE4FFF: the loader intact? the record at 0xE4F00 present, magic 0x5441, CRC valid?
   - 0x93000.., 0xDC000.., 0xFC000..: the data areas (the user sounds and settings are already backed up).
   Say what that shows about why the SPL did not start the loader at power-on.
7. `python3 tools/fm1t.py write --package FM-1.fwsc --ref backup.bin` **without** `--write` first (it only
   checks), then with `--write`. The official V15 first; ChoralRoot afterwards through the normal installer.
8. Power-cycle the FM-1 (unplug the Transporter). It should start stock V15. Then
   `python3 tools/fm1_install.py build/choralroot.fwsc --yes`, on a full battery, with the Mac kept awake
   (`caffeinate -dimsu`), and the Transporter within reach.

**Rules.** The boot head 0x0000..0x3FFF is never written. No chip erase. A double-read backup before any write. The
user data (user sounds U01 SHIMMER, U02 TINE EP, settings) is backed up on the Mac
(`choralroot-backup-20261007.json`), so erasing data areas is acceptable if a guide calls for it.

**Follow-ups for the project, after the unit is back.**

- A battery check before an install: `song.batt_raw` exists (`firmware/src/main.c`, thresholds 531 / 561 / 591 in
  `cr_ui.c`, shown as full on USB power) but the installers cannot read it; the loader and other firmware cannot
  report it at all. At least a "charge it first" line in the install docs.
- An interrupted write has no fallback that survives a warm reset. Fix what the dump shows (the flash record, or the
  RAM record cleared too early), and test the power-loss resume on hardware.
- INSTALL-COMPAT.md's "If an FM-1 is dark" checklist: add this case (no USB device at all after an interrupted
  loader write), and that a power-on clears the reworked boot guard, so quick restarts do nothing there.

---

## Preparation done on the Mac (2026-10-07, before the parts arrived)

- **Transporter firmware built**: `../FM-1-transporter/build/fm1_transporter.uf2` (219 648 B; pico-sdk 2.2.0 at
  `~/pico-sdk`, Arm GCC 9.2.1, CMake 3.27; the wl82loader.bin from `../jl-uboot-tool` at adb3f18, sha256 d41da612...,
  embedded: `build/generated/wl82loader.h`). picotool is not installed: flash the XIAO by holding its BOOT button while
  plugging it in and copying the .uf2 onto the `RPI-RP2` disk. pyserial 3.5 is installed for `tools/fm1t.py`.
- **The review gate**: `fm1t.py write` imports `fm1_ota.require_reviewed` and `fm1fw.Firmware` from fm-1-research-lab,
  which is not public. Our stand-in is `tools/transporter/` (README there; `FM1_RESEARCH=$PWD/tools/transporter`):
  it accepts the official V15 (identity FM-1_015, the Unbricker's region hash) and ChoralRoot packages (FM-1_920 with
  the Felucca loader marker; the head compared with a V15 package when `FM1_V15` names one). The flash image is the
  package's type-0 entry at the offset the ciphered UFW entry list gives (0x400 in our packages; `ota.c ota_ufw`).
  Test: `python3 tests/transporter_shim_test.py`.
- **Still needed**: the official V15 `FM-1.fwsc` from https://www.m-vave.com/download (not on this Mac; check it with
  `python3 ../MvaveFM1Unbricker/fm1_unbrick.py extract FM-1.fwsc app_v15.bin --verify-v15`), and the parts.
- Docs updated: `docs/INSTALL-COMPAT.md` ("Before an install", checklist item 0), README ("Before any install").
- The loader analysis stands as written above; `fm1_updata_parm_clear` (hal/fm1_sys.h) zeroes the RAM record only,
  so the flash record at 0xE4F00 should have survived: the dump decides whether the SPL honours it.
- **The Unbricker was wrong and is fixed** (MvaveFM1Unbricker f6e1bc2, pushed): it sliced the logical image from
  0x4000 instead of the package's flash.bin (at 0x400 through the UFW entry list), so a `restore` would have written
  bytes shifted by 0x400. Its V15 hash identifies the genuine file (the logical slice); the flash-region hash
  `V15_FLASH_FW_SHA256` is unpinned until `extract FM-1.fwsc out.bin --verify-v15` runs on the genuine file. Check
  u/acrawf1's `fm1_extract_app.py` for the same bug before trusting it.
