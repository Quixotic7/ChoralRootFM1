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
| Felucca 1.0 or later | `FM-1_9XY`, INFO `FELUCCA v1.0`, `v1.0.1`, `v1.1-rc1` ... | **allowed** |
| Melodee | `FM-1_9...`, INFO `MELODEE v0.11.1` ... | **allowed** |
| ChoralRoot | `FM-1_920`, INFO `ChoralRoot 0.12` ... | **allowed** |
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
   crashes on every boot lands there every time and looks bricked. The Reddit fix (erase 0x97000+0x8000 and
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
     SAMPLE / GRAIN ones become INIT on ANALOG.
   - Static stack: the worst boot path is about 3.2 KB of a 24 KB stack.

**Conclusion.** Not the loader: Sloop's loader handles our package correctly (and is not the one used), the write
is byte-correct, the formats match. Not reproducible on the host either: ChoralRoot boots on Sloop's and 0.9-beta's
data in the emulator. The cause is unknown. Sloop therefore stays **refused** (unknown is refused). The message names
the observed outcome, not a mechanism.

**Open.**
- Device-only paths: real flash reads through the XIP window and the JEDEC probe, guard faults, USB / panel / ADC
  start, `ota_boot_cleanup`'s erases with IRQs off.
- Which build the bricked unit was given.
- The boot guard. ChoralRoot's (`main.c` `fm1_cstart`, inherited from Felucca 1.0) counts every reset, power-on
  included, and enters ROM boot ("WL82 UBOOT1.00") at the third boot after two resets that each came within 30 s.
  Sloop's clears on a power-on reset. So quick restarts alone could reach ROM boot while .noinit RAM survives, with
  no data problem at all; an install's restarts count too. Worth fixing in `firmware/src`: clear the guard on a
  power-on reset as Sloop does.
- Whether returning to V15 first changes anything. Stock's VM region is 0x93000..0xE9000;
  0xFC000..0xFEFFF lies outside it.

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
