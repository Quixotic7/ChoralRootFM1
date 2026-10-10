# Boot safety: a way back to UBOOT when a firmware upload fails (design, 2026-10-07)

The user's ask after the dark unit: *there should always be a quick, simple way to get the device back to UBOOT if an
upload fails*, and the current "hold OCT- and OCT+ for 10 s" is too long. This page is the design; the pieces that
need the real unit (the dump, two experiments) are marked. Nothing here is implemented yet. **Decided 2026-10-07: wait for the Transporter's dump and the two experiments before building any of it** (the SPL's behaviour may change the design).

## What we have today, and where it failed

| layer | what it does | survives a half-written app? |
| --- | --- | --- |
| the chip's ROM | UBOOT1.00 over USB (the WL82 disk) when the SPL asks for it, or when a USB_KEY is sent on the data lines at power-on (the Transporter) | yes, always; but only the Transporter can trigger it from outside |
| the SPL (`uboot.boot`, flash 0x0000..0x3FFF, never written) | boots the app at 0x4000; honours an UPDATA_PARM record (RAM at 0x01C7FD88; a flash copy at 0xE4F00 written by `ota.c`) to run the update loader at 0xE0000 instead | unknown on hardware: the dark unit says the flash record was not honoured, or the loader did not run (the dump decides) |
| the update loader (0xE0000..0xE4FFF, Felucca's, staged before every update) | USB-MIDI `ota-FM-1`, writes the app area 0x4000..0x93000 sector by sector from low to high, 8 s watchdog; clears the RAM record at its own start | yes, if the SPL starts it |
| the app (0x4000..0x93000) | arms the WDT first thing (`fm1_cstart`), the boot guard (two crash boots -> SAFE MODE, four -> UBOOT), OCT- + OCT+ at power-on = calibration, held 5 s in the main loop = UBOOT, the SysEx soft key | **no**: every one of these lives in the app. With sectors 0x4000..0x25FFF new and the rest old, the entry sector is valid-looking and the code behind it is a mix; the chip jumped into it and hung before anything we own ran (or the SPL rejected it and hung: see the experiments) |

So the only safety that worked was the hardware one, and it needs a board we did not have.

## What the dump showed (2026-10-09, the dark unit read through the Transporter)

Two full reads, identical (sha256 `6c075920…`; `docs/TRANSPORTER-HANDOFF.md`, "Result"). Against the V15 package, the
package that was being installed (commit `23a8a75`, rebuilt: its hash is the one the PC had, `e59c47ee…`, so the build
is reproducible) and the build before it:

| region | finding |
| --- | --- |
| 0x0000..0x3FFF | identical to V15's head |
| 0x04000..0x24FFF (33 sectors) | the new package, bit-exact |
| 0x25000..0x92FFF (110 sectors) | the old firmware, every sector fully programmed: **no torn sector**. The loader died between finishing 0x24000 and erasing 0x25000 (33 sectors = the 274 requests, 8 reads per sector) |
| 0xE0000..0xE4FFF | the staged loader **intact**: outer header CRC valid, body byte-identical to `build/loader/ota.bin`, marker at 0xE004C |
| 0xE4F00 | the record **present and valid**: CRC, 0x5A0D / 0x5A01, magic 0x5441, `ota-FM-1_015`, area 0xE0000; the only record in 0x93000..0xFC000 |

**Experiment 1 is answered: the SPL does not start the loader from the flash record at 0xE4F00 on a cold boot.**
Everything the resume path needs was in flash and valid, and the unit never produced `ota-FM-1`. (The dump cannot
separate "the SPL ignores the record" from "the SPL runs the loader and the loader dies on a cold start", but the
result is the same: the flash record is not a resume path.) The RAM record is the only hand-over known to work, and a
power cycle clears it. `ota.c`'s flash copy of the record stays (it is harmless and `ota_boot_cleanup` erases it), but
nothing may rely on it. This settles the design below: the stub is the only safety that survives a power loss.

Decisions taken from this:

- `FELUCCA_OTA_RAMONLY` stays 0 (the flash record costs nothing), but the docs and the installer stop promising a
  resume after a power loss; "charge first, keep the computer awake" is the rule (`INSTALL-COMPAT.md`, README).
- The stub + write order (next section) is the work item, in the order of §5. The two experiments left (§4, 2 and 3)
  are now safe to run: the Transporter restores anything, and `MvaveFM1Unbricker/fm1_transporter_recover.py` does the
  dump / restore in one go. Run them before the stub's hand-over code is written (experiment 3 decides how the stub
  starts the loader).
- The Transporter's dump is also the test oracle for the loader's host model (`tests/ldr_test.c`): the "33 new, then
  old, nothing torn" state is the fixture the stub must turn into a rescue.

## The design: a stub that owns the first sector, and a write order that keeps it true

### 1. A boot stub in sector 0x4000, version-independent

The app's first 4 KiB sector becomes a small **stub** (our code, built once, changed rarely) that the SPL enters as
it enters the app today (the `app_area_head` / `app_dir_head` layout of `tools/fm1pkg_make.py` and Felucca's
`crt0`: the stub keeps that entry convention and the header fields the SPL reads). It:

1. arms the watchdog (as `fm1_cstart` does now, first instruction);
2. scans the key matrix for ~50 ms (Sloop's `recovery_key`) and reads the reset reason;
3. checks the **commit marker** of the app body: the body's last sector (0x92000..0x92FFF) holds `magic, version,
   length, CRC-32 of 0x5000..0x92000, CRC-16 of the marker`; the stub recomputes the CRC-32 over the body (about
   0x8D000 bytes from XIP: a few tens of ms);
4. decides:
   - marker valid and no rescue key -> jump to the app's real entry (the body's first sector, 0x5000);
   - **rescue key held** (OCT- alone at power-on, Sloop's gesture; the user's "quick, simple"; no 5 s or 10 s) or
     marker invalid -> **rescue**: if the loader area 0xE0000..0xE4FFF holds a valid loader (it has a header and a
     CRC: `ota.c ota_stage` checks them when it stages it; the stub checks the same) run it: `ota-FM-1` appears on
     USB-MIDI and the normal installer resumes or re-installs, no extra tool; else enter the ROM's UBOOT
     (`fm1_enter_uboot`: the WL82 disk, for the Unbricker / Transporter).

Nothing in the stub needs the app, the settings or the flash data. The stub is the one thing that has to be right,
so it is tiny, it has host tests (`tests/`, as `cr_bootguard_test.c`), and the loader writes it only when its bytes
differ from what is in flash (a stub change is a rare, announced event).

### 2. The loader's write order

Today: sectors 0x4000 -> 0x92000 in order, each verified. New order, so that **every interruption leaves a state the
stub turns into rescue**:

1. erase the marker sector 0x92000 first (the body is now "uncommitted": the stub would rescue);
2. write the body 0x5000 .. 0x91FFF, sector by sector, verified (as today);
3. write the marker sector last (CRC-32 of what was just verified);
4. only if the package's stub differs from flash: erase and write sector 0x4000, verified (the one window where an
   interruption leaves no stub; the installer says "do not unplug" with emphasis there, and it is a few hundred ms).

An interruption in 1..3 leaves the old stub + an invalid marker: power on -> rescue (the loader or UBOOT). A
power-on with an intact old app and no marker (a unit updated by an older loader, or Felucca / Melodee data) must
still boot: the stub treats "no marker sector at all, body CRC unknown" as **boot the app** only when the body's
first sector looks like a Felucca-family entry (its header magic); the first ChoralRoot install with the new loader
writes the marker and from then on the rule is strict. (To be decided with the experiments below: whether the SPL
itself verifies the `app.bin` CRC-16 in the app directory; if it does, the marker is already there in spirit and the
stub's check is what happens *after* the SPL accepts.)

### 3. The gestures, shortened

| gesture | today | proposed |
| --- | --- | --- |
| OCT- alone held while switching on | nothing | **rescue** (the stub, before the app): the loader on USB-MIDI, "RESCUE · connect USB · open the installer" on the screen if the LCD init is cheap enough in the stub, else the LEDs only |
| OCT- + OCT+ held while switching on | calibration | calibration (unchanged, the app's) |
| OCT- + OCT+ held 5 s in the main loop | countdown, then UBOOT | unchanged as a fallback, but no longer the documented way; the docs say "hold OCT- while switching on" |
| the SysEx soft key from the installer | UBOOT | unchanged |
| the boot guard | 2 crashes -> SAFE MODE, 4 -> UBOOT | unchanged, and now reachable only when the stub has handed over to a valid body |

### 4. What only the unit can tell us (with the Transporter at hand, the experiments are safe: it restores anything)

1. **The dump** (`docs/TRANSPORTER-HANDOFF.md` step 6): is the flash record at 0xE4F00 intact and valid? Is the
   loader at 0xE0000 intact? Which sectors are new? That says whether the SPL honours the flash record at all. If it
   does not, the RAM record is the only hand-over and resume-after-power-loss is impossible by that route, which
   makes the stub the only safety and settles the design. **Done 2026-10-09: it does not (see "What the dump showed").**
2. **What the SPL does with a bad app**: with a known-good flash and the Transporter connected, corrupt one body
   sector (not the first) and power on: does the SPL (a) jump anyway and the app crashes (then the WDT + guard path
   should have reached UBOOT in the dark unit, so something else happened), (b) refuse and hang (the dark unit's
   behaviour), (c) refuse and enter UBOOT? Then the same with the first sector corrupted. The answers fix whether the
   SPL checks the app directory's CRC-16 and what it does on failure.
3. **Does the SPL honour the RAM record after a WDT reset?** (It should: that is how the soft key works.) Needed to
   know whether the stub can hand over to the loader by writing the RAM record + core reset (the simplest way to run
   the loader: no need to parse and jump into it ourselves).

### 5. Work items (after the unit is back)

- `firmware/stub/`: the stub (C + crt0, `.ld`), its host test, and its place in `tools/fm1pkg_make.py` (sector 0x4000
  = the stub; the body from 0x5000; the marker sector at 0x92000; `app.bin`'s CRC-16 over the whole as today).
- `firmware/loader/ldr_core.c`: the write order above; a `--stub` log line; `tests/ldr_test.c` for the interruption
  cases (power loss after k sectors -> the stub rescues: a host model of stub + loader + SPL hand-over).
- `firmware/src/main.c`: the 5 s path kept, the docs changed; the install texts (page, CLI, README,
  INSTALL-COMPAT.md "If an FM-1 is dark") say "hold OCT- while switching on".
- The installers: "do not unplug" emphasised during step 4; a stub-changed warning.
- Felucca upstream: the write order and the stub are worth offering back (issue #61's and this unit's class).

### Non-goals

- A second firmware bank (A/B app slots): the flash map has no room for two apps beside the data.
- Changing the head 0x0000..0x3FFF (SPL, isd_config): never written, by every tool's rule.
