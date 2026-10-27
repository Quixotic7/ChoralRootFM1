# Which firmware ChoralRoot installs over

An FM-1 that ran **Sloop** (a fork of Felucca 0.9-beta) ended in the chip's ROM boot mode (black screen, the
computer shows a USB disk "WL82 UBOOT1.00", USB 4C4A:8057) after ChoralRoot was installed over it. A Reddit guide
records the same symptom after **Felucca 0.9-beta -> 1.0.1**: the flash dump showed the new firmware written
byte for byte, and the device came back once the old data (0x97000.., 0xFC000..) was erased. This page is what the
host checks found, and the policy the installers follow (`web/fm1ota.js` `classifyFirmware`,
`tools/fm1_install.py` `classify_firmware`).

## Policy

| running firmware | how it is recognised (handshake identity / INFO version) | install ChoralRoot? |
|---|---|---|
| official M-VAVE firmware (V15 and older) | `FM-1_001`..`FM-1_099` (V15: `FM-1_015`); no INFO | **allowed** |
| Bubba Machine | the MIDI port "FM-1 Bubba Machine" (its handshake: `FM-1_003`); no INFO | **refused**, also with `--force` / the override ("Bubba Machine" below) |
| Felucca 1.0 or later | `FM-1_9XY`, INFO `FELUCCA v1.0`, `v1.0.1`, `v1.1-rc1` ... | **allowed** |
| Melodee | `FM-1_9...`, INFO `MELODEE v0.11.1` ... | **allowed** |
| ChoralRoot | `FM-1_920`, INFO `ChoralRoot 0.12` ... | **allowed** |
| RemoteFM1 0.2 and later | `FM-1_52XXYYZZ` (release X.Y.Z, two digits each: 0.2.1 `FM-1_52000201`), dev builds `FM-1_5200`; no INFO | **allowed** (RemoteFM1 below) |
| Sloop | INFO `FELUCCA SLOOP 2.2` (dev build), `FELUCCA 2.2 BETA` (release build) | **refused** |
| Sloop's rescue mode | `FM-1_000` | **refused** |
| Felucca 0.x betas (0.9-beta and older) | `FM-1_909` ..., INFO `FELUCCA 0.9-BETA`, `FELUCCA 0.5 BETA` | **refused** |
| anything else | a 9xx identity without INFO, another number, another INFO text | **refused** |
| a device in update mode | `ota-FM-1_...` | the install / resume finishes the write |

The 9xx identities cannot tell the firmwares apart: every Felucca-based build derives `FM-1_9XY` from its release
number (Felucca 0.9-beta `FM-1_909`, Felucca 1.0 `FM-1_910`, Sloop 2.0 `FM-1_920`, which is ChoralRoot's own), and
the dev builds are all `FM-1_900`. So after the handshake both installers send the backup protocol's INFO
(`web/EDITOR_PROTOCOL.md`; read-only) and classify by its text. The stock firmware does not answer INFO, which is
fine: it is recognised by its `FM-1_0NN` identity.

A refusal says:

> Installing over *NAME* is not supported: An install over it has left an FM-1 that no longer starts, and the data it leaves in the flash is not known to be safe for ChoralRoot. Return to the official V15 firmware with the installer you used
> for *NAME* first, then install ChoralRoot. If an FM-1 is already dark (black screen, a "WL82 UBOOT1.00" USB disk):
> https://github.com/Quixotic7/MvaveFM1Unbricker

Overrides: `tools/fm1_install.py --force` (exit code 8 without it); on the page an "I understand the risk" box that
appears only after a refusal, then a second confirm. The return to the official V15 (the page's "Return to official
V15", or `fm1_install.py FM-1.fwsc`) is never refused: it is the way out.

## RemoteFM1 (2026-10-27)

RemoteFM1 (github.com/Quixotic7/RemoteFM1, the FM-1 as a panel for a computer) 0.1 answered as the official V15
(`FM-1_015`) and was installed over as stock. 0.2 on is built on fm1-base + felucca-bsp and answers with fm1-base's
identity: its `fm1.toml` `id_prefix` `FM-1_52` + `00` (a development build) or + XXYYZZ (a release; fm1-base
`tools/build.py --release`). It does not answer INFO (no `backup` in its `fm1.toml`), so the page asks before
installing without a backup, as over V15. Its updater is felucca-bsp's `ota_stage`, which stages the package's own
loader (any well-formed one; ours carries `FELUCCA-LOADER-1`), the same path as over Felucca 1.0. Only RemoteFM1's
prefix is allowed: other fm1-base firmwares (FIZZ `FM-1_46...`, ...) stay "unknown" until their path is checked.

## Bubba Machine (2026-10-17, HISTORY 3y176)

[Bubba Machine](https://github.com/Erbubar23/bubba-machine) (a groovebox firmware, GPL-3.0) answers the handshake as
`FM-1_003`, which on its own reads as an old official firmware, and calls its USB device "FM-1 Bubba Machine". The
installers tell it apart by that port name (`classify_firmware(identity, info, port)` / `classifyFirmware(identity,
info, port)`); the same identity on the FM-1's own port "FM-1" is still the stock firmware. Its updater
(`hal/fm1/fm1_update.c` there) speaks M-VAVE's protocol, but it takes only a package whose update loader is
M-VAVE's own (`usb_hid_ota.bin`, header CRC 0xEBAA, data CRC 0x5881, 0x4DE1 bytes) and whose `flash.bin` head
0x0000..0x3FFF is the unit's: ChoralRoot's, Felucca's and every Felucca-family package carry the Felucca loader, so it
refuses them after the first reads, erases its staging area and writes nothing. That is the "no way back to any other
firmware" a player meets. So the installers refuse an install over it at once, also with `--force` or the override
(exit 8), and say the way out:

- **the official V15 goes through**: `python3 tools/fm1_install.py FM-1.fwsc` (M-VAVE's file, sha256 `db1642b2...`),
  the page's "Return to official V15", or Bubba Machine's own installer ("Firmware de fabrica"). On the owner's unit
  (Bubba Machine 0.8.0-beta, 2026-10-17): 74 reads (the package's header, the loader, the head), Bubba restarts into
  M-VAVE's loader `ota-FM-1_015`, which writes V15; "done: the FM-1 runs FM-1_015", the port "FM-1" again. Then
  ChoralRoot installs over V15 as usual.
- Bubba Machine keeps its own data in the settings area, which no installer rewrites: back on V15 the presets may
  read garbled, as after Felucca (System > Reset Patches; MvaveFM1Unbricker `repair-settings`).
- If Bubba no longer starts: holding OCT- and OCT+ for 5 s (or four failed starts) puts the chip in its ROM boot mode,
  `WL82 UBOOT1.00`, where MvaveFM1Unbricker's `fm1_unbrick.py restore` works (Windows or Linux); the Transporter
  otherwise.

## Diagnosis

Host evidence (2026-10-07; Sloop f2b44c2 built from a scratch copy with this repo's `tools/sdk`, ChoralRoot 0.12 dev):

1. **The loader that writes is the incoming package's, not the running firmware's.** `firmware/src/ota.c`
   `ota_stage` (identical in Sloop, Felucca 0.9-beta, 1.0, Melodee and here, apart from identity text) reads the
   package's `ota.bin`, accepts it if it is the official loader (known CRCs) or carries `FELUCCA-LOADER-1`, and
   stages *that* loader at 0xE0000. So Sloop -> ChoralRoot was written by ChoralRoot's (= Felucca 1.0's) loader, and
   Felucca 0.9-beta -> 1.0.1 by 1.0.1's. Harness A (Sloop's step 1 with our package): `ota test passed`,
   "staged body == the package's loader ok".
2. **The write is correct.** B (our loader, Sloop installed -> ChoralRoot): `rc 0, 1148 requests, 133 sector
   erases`, "app area == the new package's flash.bin ok", "flash head [0, 0x4000) untouched ok", `loader test passed`.
   C (Sloop's own loader with our package, the hypothetical path): all 14 checks ok incl. Sloop's added CRC pass.
   D (our loader writing Sloop) and E (our step 1 with Sloop's package): passed.
3. **The package formats are the same.** Both: UFW header `...AC791N`, flash.bin at 0x400 length 0x93000, ota.bin
   type 100 with the marker, CRC16s valid, chip key 0x980F, APP_SLOT 0x8DFBC, and a byte-identical flash.bin head
   [0, 0x4000) (SDK SPL + isd_config), which the Felucca loader never writes anyway. Sloop's fm1pkg_make.py differs only
   by a size assertion.
4. **Loader code, 0.9-beta vs 1.0:** `git diff e5a908d 727f272` in Felucca: `ldr_core.c` one comment line, `ota.c`
   comments only, `fm1pkg_make.py` docstring and one assertion. Sloop's loader is *newer* than 1.0's: it adds a CRC
   check of everything served (3 passes), no-"success" keeps the record, and a JEDEC check (0x856014) that drops the
   update if the flash chip differs. Melodee's equals Felucca 1.0's.
5. **What does go to ROM boot: the new firmware's boot guard.** ChoralRoot (and Felucca 1.0) `main.c` `fm1_cstart`:
   a `.noinit` boot guard counts boots that crash or hang (watchdog) within 30 s; at the second it calls
   `fm1_enter_uboot()`, i.e. "WL82 UBOOT1.00". Unlike Sloop's guard it is not cleared on power-on, so a firmware that
   crashes on every boot lands there every time and looks bricked. (Reworked since: see "Mitigation" below.) The Reddit fix (erase 0x97000+0x8000 and
   0xFC000+0x2000, then 1.0.2 boots; later traced to 1.0.x converting 0.9 DIGITAL sounds to FM6 at boot) fits this.
6. **The data that stays.** Neither loader touches 0x93000.. (data). Sloop's store has the same layout as Felucca's
   (settings 0xFC000, projects 0x97000.., user preset banks 0xDC000.., samples 0xA0000..) plus its own
   `OBJ_AUTOSAVE` = object 7 at 0x9F000 / 0xFE000, which is exactly ChoralRoot's `OBJ_FM6BANK` (object 7, same
   sectors). ChoralRoot's boot (`cr_shim.c` `persist_boot`: `cr_bank_boot` = user preset banks with the DIGITAL ->
   FM6 conversion inherited from Felucca 1.0, the FM6 bank; `cr_settings_boot`) reads Sloop's objects. The FM6 bank
   path rejects a foreign payload (magic, version, size checks); the user-preset DIGITAL conversion and the settings
   import are the Felucca 1.0 code that crashed on 0.9-beta data.

7. **Host boot on old data: no crash.** ChoralRoot's emulator, built with ASan and UBSan from three trees (the
   v0.11 tag, HEAD and the working tree), booted for 33 s on 1 MiB images filled by Sloop's own code (32 user records
   on every Sloop engine including DIGITAL, PER3 settings, 4 FUN4 projects, the OBJ 7 autosave, samples in all 3
   slots) and by Felucca 0.9-beta's. It also ran 61 s of preset and algorithm browsing, and a second power-on. Every run
   returned 0, with no out-of-bounds report; only DSP shift/overflow warnings that also appear on an erased image.
   - Sloop's autosave as the FM6 bank: rejected (magic). Its PER3 settings: no size match, defaults used.
   - Sample data at 0xC8000: ignored as loops. All 32 user records accepted. DIGITAL ones are converted to FM6;
     SAMPLE / GRAIN ones become INIT on ANALOG (since 0.14: on the VA; ANALOG ones play on the VA with their macros).
   - Static stack: the worst boot path is about 3.2 KB of a 24 KB stack.

**Conclusion.** Not the loader: Sloop's loader handles our package correctly (and is not the one used), the write
is byte-correct, the formats match. Not reproducible on the host either: ChoralRoot boots on Sloop's and 0.9-beta's
data in the emulator. The cause is unknown. Sloop therefore stays **refused** (unknown is refused). The message names
the observed outcome, not a mechanism.

**Open.**
- Device-only paths: real flash reads through the XIP window and the JEDEC probe, guard faults, USB / panel / ADC
  start, `ota_boot_cleanup`'s erases with IRQs off.
- Which build the bricked unit was given.
- The boot guard: reworked (below). Felucca 1.0's counted every reset, power-on included, and entered ROM boot
  ("WL82 UBOOT1.00") at the third boot after two resets that each came within 30 s, so quick restarts alone could
  reach ROM boot while .noinit RAM survived, with no data problem at all.
- Whether returning to V15 first changes anything. Stock's VM region is 0x93000..0xE9000;
  0xFC000..0xFEFFF lies outside it.

## Mitigation: the boot guard rework (2026-10-07)

`firmware/src/cr_bootguard.h` (`bootguard_step`, used by `main.c` `fm1_cstart`; host test `tests/cr_bootguard_test.c`):

- **The reset reason decides.** `hal/fm1_sys.h` `fm1_reset_reason` snapshots P3_RST_SRC (bit0 power-on, 1 VDDIO low,
  2 WDT, 3 VCM, 4 long press, 5 1.2 V, 6 soft via P33) and RST_SRC (bit5 soft via PWR_CON). WDT, or a soft reset
  (`fm1_fault`'s reboot after a crash screen is one), counts as a failed boot when the previous boot had not run 30 s
  and had not announced an intentional reset (UBOOT, an update's commit, a reboot asked for clear `pending` first). A
  power-on clears the guard, as Sloop's does. Brown-outs, VCM and the long press are never counted.
- **SAFE MODE before ROM boot.** Two failed boots in a row start the firmware without reading any flash object: no
  settings record (defaults), no user sounds, no VA / FM6 stores, no FM6 bank, no loops, and nothing is written (the
  stores stay as they are). The instrument plays its factory sounds; the installer (M-UPGRADE) and the backup's
  reads work (a backup can be taken before erasing); the update gesture still works (OCT- + OCT+ + HOME held 5 s
  from 0.18 on, OCT- + OCT+ until 0.17). The screen says
  SAFE MODE with the boot stage the crash reached (`felucca_dbg.stage`, kept in .noinit), the top line "Safe mode",
  and Options opens on **Safe Mode** ("flash data skipped", "OCT- OCT+ HOME 5 s: update"; until 0.17 "OCT-+OCT+ 5 s: update mode") and **Flash Data**
  ("erase and reboot", OCT+ twice: erases the stores, the FM6 bank, the loops, the user sounds and the settings,
  not the firmware, then reboots normally). A power cycle instead boots normally with the loads; if they crash
  again, the unit comes back to SAFE MODE, not ROM boot.
- **ROM boot only when SAFE MODE fails too**: four failed boots in a row (SAFE MODE itself crashed twice).
- **Diagnostics**: the console `boot` (the mode, the reset reason, the last stage and the stage names); GEEK OUT's
  third line `boot <reason> stage <n>` after an unclean boot.

What only a device can confirm: that a power-on sets P3_RST_SRC bit0 without bit2 / bit6 (the bits are documented in
the HAL but nobody has dumped them across a power cycle, a watchdog and a soft reset; if bit0 stays set after a
watchdog reset, WDT still wins; if bit2 or bit6 stayed set across a later power-on, a quick power-off within 30 s would
count, as in 1.0, but would end in SAFE MODE, not ROM boot), and that SAFE MODE boots on a unit whose crash is in
the flash data.

### Before an install (learned the hard way, 2026-10-07)

The update loader writes the app area sector by sector over USB-MIDI with an 8 s watchdog. Up to and including 0.15
there was no fallback: if the FM-1 reset or dropped off USB mid-write, the app was half old, half new, the SPL jumped
into it (it checks nothing, and it does not start the loader from the flash record on a cold boot: BOOT-SAFETY.md)
and the unit was dark. **From 0.16 on, the first version with the boot stub** (docs/BOOT-SAFETY.md "As built"), the app's
first sector is a stub that boots only a body the loader has committed, and otherwise hands over to the staged
loader: an interrupted install comes back in update mode at the next power-on. Not covered: the stub sector's own
write, which happens once, in the first install of a version with the stub (about 16 page writes out of ~1900), and
again only if a release changes the stub (announced in its notes).
So: **charge the FM-1 fully first** (it runs on its battery while updating; the installers cannot read its battery),
plug it **straight into the computer** with a cable known to carry data (no hub), keep the computer awake
(`caffeinate -dimsu` on a Mac), and do not touch the cable until "done" is printed.

### If an FM-1 is dark (a user's checklist)

**First, on a ChoralRoot with the boot stub** (from 0.16 on; 0.15 and older do not have it, and
no other firmware does): switch off, **hold OCT- alone, switch on**. Three short flashes of the key LEDs (the first
key column), the screen black: the FM-1 is in update mode (`ota-FM-1` on USB-MIDI, "in update mode" in
`fm1_install.py --info`); open the installer and press Install. A unit whose install was interrupted does this
without the key; a working unit does it with the key (the firmware keeps the update loader of its last install
stored for this; switching off and on starts the firmware again, nothing was written).
One long flash: switch off for 10 s and try once more (a second try straight after a first counts as a failed
hand-over). One long flash again: no update loader is stored (a unit that has not been installed by a version that
keeps it, or whose last install step was refused half way): the FM-1 is the "WL82 UBOOT1.00" disk until the next
power cycle (then step 3's Unbricker, or simply switch off and on to start the firmware if it was whole). No flash
at all: no stub runs; go on below.

0. **If the install was interrupted mid-write** ("the loader was disconnected after N requests") and afterwards the
   computer sees *nothing at all* (no FM-1 port, no `ota-FM-1`, no WL82 disk): this happened on 2026-10-07 to the
   author's unit (274 of ~1167 requests; black screen; no USB device in any mode on a Mac and on Windows; the OCT hold,
   two minutes on, three quick restarts and an hour of charging changed nothing). Note that the reworked boot guard is
   cleared by a power-on, so quick restarts cannot drive it into ROM boot. The route is the hardware one, the FM-1
   Transporter: a Seeed XIAO RP2040 on the FM-1's USB data lines (three wires, no VBUS) that forces the chip's ROM
   boot at power-on. **Resolved 2026-10-09 that way** (`docs/TRANSPORTER-HANDOFF.md`, and the step-by-step guide with
   the parts, wiring and script in [MvaveFM1Unbricker's TRANSPORTER-GUIDE.md](https://github.com/Quixotic7/MvaveFM1Unbricker/blob/main/TRANSPORTER-GUIDE.md)).
   The dump showed: head intact, 33 new sectors in front of 110 old ones, no torn sector, the staged loader and the
   flash record at 0xE4F00 both present and valid, and still no `ota-FM-1` at any power-on. So the SPL does not run the
   loader from the flash record on a cold boot: nothing the app or the loader leaves in flash brings a half-written
   unit back, and none of steps 1-3 below can work on it because no code of ours runs. Official V15 was written through
   the Transporter, the unit power-cycled to stock, then ChoralRoot installed normally on a full battery.

1. **Leave it switched on, on USB, for 2 minutes** and watch. A unit in ROM boot stays black and shows the
   "WL82 UBOOT1.00" disk (USB 4C4A:8057) the whole time. A unit whose firmware crashes and restarts flashes its
   backlight or LEDs every few seconds: with this firmware it then settles in SAFE MODE (the yellow screen) after the
   second crash; Options > Flash Data erases what it trips on.
2. **Hold OCT- and OCT+ while switching on, and keep holding for 10 s.** If the firmware runs, this starts the
   calibration screen and, up to 0.17, 5 s into the main loop the countdown, then "UBOOT" (the ROM's USB boot): the
   firmware runs, the install can be repeated. From 0.18 on the held gesture needs HOME as well (OCT- + OCT+ + HOME,
   5 s: the countdown, then update mode through the boot stub, where the installer can finish; the ROM's USB boot
   only on a unit whose stub did not run): hold the three once the calibration screen shows. Nothing happening
   (still black, no disk change) means the firmware does not reach its main loop.
3. **Check what the computer sees.** Windows: Device Manager (View > Devices by connection), under Disk drives a
   "WL82 UBOOT1.00" disk = ROM boot (use [MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker)); under
   Sound, video and game controllers an "FM-1" = the firmware runs (the web installer can reach it); nothing new on
   plugging in = try another cable and port, then the hardware route
   [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter). macOS: System Information > USB, the same
   three cases.

## Several FM-1s (the unit ID, 2026-10-14, HISTORY 3y66)

Every FM-1 calls itself the same: the port "ChoralRoot FM-1", the identity `FM-1_920`, and on a restart into update
mode "Felucca Update" / `ota-FM-1_900`. With two units on one computer a tool that takes the first port that answers
can talk to the wrong one, and an install that finds the unit again "by name" after its restarts can follow the other.
ChoralRoot therefore gives each unit an ID.

**The unit ID.** 8 hex digits, `A1B2C3D4`: the CRC-32 (zlib's) of the SPI flash's 128-bit unique ID. The firmware
reads that ID once at power-on, right after the JEDEC id it already checks, with the same sequence the JieLi AC79 SDK
uses at every power-on of an SDK firmware such as M-VAVE's (`cpu.a` `vm_sfc.c read_flash_id`: `9Fh`, then `4Bh`, four
`00h` bytes, 16 bytes read): a read-only command, from RAM, with XIP off and the interrupts masked, as every flash
command of `hal/fm1_flash.h` (`fl_uid_ram`). Nothing is written, nothing is stored: the ID is the flash chip's, so it
stays the same across installs, restores and Reset Settings. Not read in SAFE MODE (that boot runs as before: no ID),
and a flash that answers sixteen equal bytes has none. The update loader and the boot stub do not read it (their
images are byte for byte as before). Shown on the unit under **Options > Version**: the line under the version reads
`unit ID A1B2C3D4` (`no unit ID` without one).

**Over SysEx.** INFO (`cr_backup.c CRB_INFO`) appends a tagged field after ChoralRoot's `42 01 03` and `43 01 03`:
`49 08` and the ID's 8 hex digits in ASCII. Without an ID the reply is as before. The hosts of before read the version
up to its NUL (`fm1_install.py device_info`, `fm1backup.js deviceInfo`) and `editor.html` Felucca's fields from the
start, so they are unaffected; the new hosts walk the tags after Felucca's 7 bytes (`unit_id_of`, `unitIdOf`).

**fm1_install.py.**

```sh
python3 tools/fm1_install.py --info                         # every FM-1: identity, port, version, unit ID
python3 tools/fm1_install.py --info --unit a1b2c3d4         # that one (any case)
python3 tools/fm1_install.py build/choralroot.fwsc --unit A1B2C3D4 --backup ~/fm1-backups/
python3 tools/fm1_install.py --sounds --unit A1B2C3D4       # --unit goes with every command
```

- `--info` asks every candidate port and prints each FM-1 (one FM-1: the lines of before plus `unit ID: ...`). A unit
  without an ID is listed too, with why: update mode, the stock firmware, another firmware, a ChoralRoot from before
  the ID. Two FM-1s whose ports have the same name (as CoreMIDI may list them) appear as `ChoralRoot FM-1` and
  `ChoralRoot FM-1 [2]`.
- `--unit ID`: before the first port a command takes, every candidate port is asked; only the one whose INFO carries
  the ID is used. None: exit 3 with the IDs that answered; two ports with it: exit 9. Every port taken again later (a
  backup and then the install, a wait after a restart) is checked by its ID.
- An install with `--unit`: the ports are counted while the FM-1 is still there; after it is told to switch to update
  mode, only the one port that appeared new is taken as its loader, and only once its own port has gone. More than one
  new port, a new port whose name another port also has, a new port that answers as something else, or its own port
  still there beside a new one: it stops (exit 9) before anything is written; the FM-1 then waits in update mode:
  unplug the other FM-1s and run the install again without `--unit`. After the write the FM-1 is the port whose INFO
  carries the ID again, printed (`unit ID A1B2C3D4: the same FM-1 after the restart`). A package whose firmware has no
  ID (the official V15, an older ChoralRoot) is followed as the one new port of a name no other port has, and said so;
  otherwise exit 5 and the advice to check with `--info`.
- With `--unit` nothing rescans MIDI (CoreMIDI's `MIDIRestart` restarts every MIDI port of the computer, the other
  FM-1s' too): `--rescan-midi --unit` is refused, and the advice for a port CoreMIDI keeps offline is to unplug and
  replug that FM-1.
- **Without `--unit`**, when two or more FM-1s answer (asked only when two or more candidate ports exist: one FM-1 runs
  exactly as before): a command that writes, restarts or changes what an FM-1 plays (an install, `--restore`, the
  sound imports, renames and deletes, `--restore-favorites`, `--stub`, `--select`, `--screen`) is refused, exit 9,
  with the IDs; a read (`--backup`, `--debug`, `--sounds`, the exports, `--favorites`) reads the first, as with one
  FM-1, and prints a note with the IDs.
- An FM-1 in update mode has no ID: to finish an interrupted install while another FM-1 is connected, unplug the
  other one and run the install without `--unit`.

**The web pages.** The installer's "What is on my FM-1?" adds the unit ID; with several FM-1s it lists each (unit ID,
version, port) and Install, Back up, Restore, the Sounds and the return to V15 refuse (nothing written): the page
works with one FM-1 at a time. The device manager asks which FM-1 to open, by number or unit ID, and shows the ID in
its header (docs/DEVICE-MANAGER.md "Several FM-1s"). Not done: the installer asking which FM-1 and following it
through an install's restarts as `fm1_install.py --unit` does.

**Checked on the host only** (tests/install_test.py `units`, tests/cr_backup_test.c, tests/cr_draw_test.c, the web
tests). On a unit, to check once: Options > Version shows an ID, `--info` prints the same ID, and the ID is the same
after a power cycle and after an install; two units show different IDs; and how macOS names two FM-1s' ports.

## Reproducing the loader checks (manual; needs a Sloop checkout, not run in CI)

The loader harness (`tests/ldr_test.c`, `tests/ota_test.c`, in `tests/run_tests.sh`) drives a loader / the app's
step 1 against a package on the host. To run Sloop against ChoralRoot:

```sh
SP=$(mktemp -d); rsync -a --exclude .git ../sloop-fm1/ $SP/sloop/        # do not build inside Sloop's checkout
(cd $SP/sloop && AC79_SDK=$PWD/../../ChoralRootFM1/tools/sdk JIELI_TOOLCHAIN=$HOME/.jieli/toolchain sh ./build.sh)
#   (AC79_SDK: this repo's tools/sdk, absolute path; Docker running on macOS) -> $SP/sloop/build/felucca.fwsc
CC="cc -O1 -Wall -Wno-unused-function"
(cd $SP/sloop && $CC -o $SP/sloop_ldr tests/ldr_test.c && $CC -o $SP/sloop_ota tests/ota_test.c)
$CC -o $SP/cr_ldr tests/ldr_test.c && $CC -DOWN_PKG=1 -o $SP/cr_ota tests/ota_test.c
$SP/sloop_ota build/choralroot.fwsc                                     # A: Sloop's step 1, our package
$SP/cr_ldr $SP/sloop/build/felucca.fwsc build/choralroot.fwsc           # B: our loader over Sloop (the real path)
$SP/sloop_ldr $SP/sloop/build/felucca.fwsc build/choralroot.fwsc        # C: Sloop's loader, our package
$SP/cr_ldr build/choralroot.fwsc $SP/sloop/build/felucca.fwsc           # D: our loader, Sloop's package
$SP/cr_ota $SP/sloop/build/felucca.fwsc                                 # E: our step 1, Sloop's package
```

Each prints `ota test passed` / `loader test passed` (2026-10-07, Sloop f2b44c2 "SLOOP 2.2", ChoralRoot 0.12 dev).
