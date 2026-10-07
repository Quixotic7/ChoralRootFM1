# ChoralRoot FM-1: settings persistence

Files: `firmware/src/cr_settings.h` (the record), `firmware/src/cr_settings.c` (import / apply / capture, and
in the firmware unit the flash side and the UI glue), `firmware/src/settings_persist.c` (Felucca's record,
now PER5), `tests/cr_settings_test.c`, the emulator's flash in `tools/emu/emu_hal_fw.h` and `tools/emu/emu_fw.c`,
and `tools/emu/test_persist.sh`.

## Where it lives

Felucca keeps one flash object, `OBJ_SETTINGS`: an A/B sector pair at 0xFC000 / 0xFD000 (`storage.c`). Each
copy has a commit record with a sequence number and a CRC, so a torn write leaves the older copy in charge. Its
payload is `persist_t` (`settings_persist.c`), whose magic marks the layout version:

| magic | adds |
| --- | --- |
| PER1, PER2 | upstream: palette, lowcut, panel table |
| PER3 | `bold` (HOLD now lives there) |
| PER4 | favorites |
| **PER5** | **`cr_settings_t cr`: ChoralRoot's block, 192 bytes, last** |

A PER1 to PER4 record imports as before, and its `cr` block is zeroed. A zeroed block fails the block's magic,
so the ChoralRoot side reads defaults. A Felucca build (`felucca.c` → `project.c`) keeps the block exactly as
saved. Felucca's own fields stay where they were: the panel calibration table, the palette, HOLD in `bold`, and
LEDs in `zoom`.

## The ChoralRoot block (`cr_settings_t`, `CRS_SIZE` = 192 bytes)

The block has its own header:

- `magic` `CRS1`
- `version` (`CRS_VERSION`, currently 4)
- `size` (the writer's `CRS_SIZE`)
- `check`: FNV-1a over bytes 12..size

| field | range | default |
| --- | --- | --- |
| playstyle / extadd / secret | Simple–Free / Add Note, Play Chord / Off, Simple, All | Simple, Add Note, Off |
| key_on, tonic, scale | 0/1, 0..11, Major/Minor | off, C, Major |
| transpose | −24..24 | 0 |
| single, split_pc | Full Octave / Split, 0..11 | Full Octave, F (5) |
| vel | 1..127 (the keys' velocity) | 100 |
| bass_on | 0/1 | **0**: the bass is OFF at power-on (it is never saved on) |
| bass_mode, bass_voicing | Chords Only..Solo, −2..4 octaves | Chords Only, 0 |
| perform_on, perform_mode, perf_sel | 0/1, Strum..Harp, the PERF layer's entry 0..6 | off, Strum, 0 |
| sticky | latch | off |
| bpm | 20..300 | 120 |
| par[5][11] | per mode, the engine's `CR_PAR_MIN`..`MAX` | design.md §21 (= `cr_init`) |
| loop_sync, loop_quant, loop_count_in, loop_level | 0..5, 0..5, 0/1, 0..100 | Free, none, off, 100 (the looper is not built yet) |
| midi_en[3], midi_ch[3] | per stream MAIN BASS RAW | on, on, **RAW off**; channels 1 / 2 / 3 |
| clock_mode | Off / **Out** / In | **Out** (Orchid sends clock) |
| raw_sound | RAW also plays part 0 | off |
| view, motion, leds | Chord..Geek Out, Full/Calm/Off, Glow/Stock | Chord, Full, Glow |
| palette | gfx.c index, 0xFF = MOD | **MOD** |
| fx_on | 0/1 | on |
| chord_sound | PRESETS list position, 0xFFFF = the UI's default | TINE EP |
| bass_sound | the ALGORITHM position BASS tap brings (1..), 0 = the UI's default | SUB BASS |
| metro_on, metro_sig, metro_vol, loop_slot (v2) | the click, 4/4 3/4 6/8, 0..100, 0..9 | off, 4/4, 70, 0 |
| pick_roots (v3) | the engine picker's white roots: 1 choose engines, 0 play (KNOB 4 in the picker) | **1** (engines) |
| usb_out, usb_in (v4) | Options > USB Audio Out / In: the device is presented to the computer (docs/USB-AUDIO.md) | **1, 1** (both on, Melodee's); a v1..v3 record takes these (its zeros would mean off) |
| usb_level (v4) | Options > USB Level: `CRS_USB_MASTER` (USB follows MASTER) / `CRS_USB_FIXED` (USB at the full level, MASTER after) | Master |
| rsv[20] | reserve for new fields | 0 |

### Import rules (`cr_settings_import`)

`cr_settings_import` returns 1 for a current record, 2 for a migrated one, and 0 when it falls back to defaults.

1. **No defaults path:** an absent or short block, a bad magic, version 0, a size that does not fit, or a bad
   checksum gives **all defaults**. It never loops and never hangs; the test feeds it 2000 garbage blocks.
2. **Older record:** an older version, or a shorter size, is copied over the defaults. Any field it lacks keeps
   its default.
3. **Newer record:** a newer, longer record (a downgrade) keeps its first 192 bytes, which hold our fields.
4. **Range check:** every field is checked against its range. A field out of range takes its default and the
   others are kept.
5. **Lists:** `palette`, `chord_sound` and `bass_sound` are checked against the lists by the glue.

## How to add a field

1. Take its bytes from the front of `rsv` in `cr_settings.h` and shrink `rsv` by that many. `CRS_SIZE` never
   changes, and the build checks it (`crs_size_ok`).
2. Bump `CRS_VERSION`.
3. Set its default in `cr_settings_defaults`, and its range in `crs_sanitize` (`CRS_FIX`).
4. If an older record's bytes there are not zero, or zero is not the default, add a line to the
   `in.version < CRS_VERSION` block of `cr_settings_import`: `if (in.version < 2) s->x = d.x;`.
5. If the field is the engine's, map it in `cr_settings_apply` / `cr_settings_capture`. If it is the UI's, map it
   in `crs_capture` / `cr_settings_load` (the glue part of `cr_settings.c`).
6. Add a check to `tests/cr_settings_test.c`.

Never reorder or remove a field. A retired field stays in place, unused.

## Runtime

| call | where | does |
| --- | --- | --- |
| `cr_settings_boot()` | `persist_boot` (cr_shim.c), the emulator's `emu_fw_init` | the flash object → Felucca's fields (panel table, palette, HOLD, LEDs) + the block; the USB settings to `usb.c` (`ua_off`) and `fx.c` (`fx_usb_fixed`) through `crs_usb_apply`, before `usb_start` builds the configuration the computer first reads |
| `cr_settings_load()` | the end of `cr_ui_init` (**patch below**); the emulator calls it after `cr_ui_init` until then | the block → engine (IRQ off), `cr_route`, the UI mirror `cs`, motion, palette, the sounds, tempo |
| `cr_settings_poll()` | every frame (`main.c` `settings_poll` → cr_shim.c; the emulator's `emu_fw_frame`) | captures the state; a change is written once nothing changed for 1.5 s and nothing has sounded for 1 s (no voice of the parts, no chord held or latched, no scheduled note, the master output under -60 dBFS: a flash erase stops the audio for ~45 ms, docs/INTEGRATION.md Performance), never while a loop plays (`CR_SETTINGS_BUSY()`), retried 1 s after a flash error, and skipped when the bytes are unchanged |
| `cr_settings_save()` | `settings_save` (cr_shim.c): the power-on calibration, Options | save now |

Saving is driven by change detection, so `cr_ui.c` needs no save call at each setting. The only call it needs
is the load at init. **The patch for `cr_ui.c`**, at the end of `cr_ui_init()`, after `cu_set_tempo(...)` and
before `song.grid = 2;`:

```c
#if CR_HAVE_SETTINGS
    cr_settings_load();                           /* the saved settings over the defaults (docs/SETTINGS.md) */
#endif
```

`cr_settings_load` is defined after `cr_ui.c` in the unit, so `cr_ui.c` also needs a forward declaration near
its top: `static void cr_settings_load(void);`. That declaration must sit inside the same `#if CR_HAVE_SETTINGS`.
The emulator does not define `CR_HAVE_SETTINGS`; there it includes `cr_settings.c` from `emu_fw.c`. Once the
patch is in, define `CR_HAVE_SETTINGS 1` in `emu_firmware.h` too, or keep the emulator's guard: `emu_fw_init`
calls `cr_settings_load()` only `if (!crs_loaded)`.

## Power-on calibration

OCT− + OCT+ held at power-on still runs Felucca's HARDWARE CALIBRATION:

- In the ChoralRoot unit, `main.c` has its own port, `cr_panel_setup`, because `ui_input.c` is not in the unit.
  `main.c`'s `#define panel_setup cr_panel_setup` picks it, so cr_shim.c's empty stub goes unused.
- `settings_save` then writes the table into the same record.

## The emulator's flash

`tools/emu/emu_hal_fw.h` holds the 1 MiB NOR as a RAM image backed by a file. It is read at power-on, and every
erase and program is written through to the file, so a crash or kill keeps what was saved. NOR rules apply:

- an erase sets a 4 KiB sector to 0xFF;
- a program only clears bits and must not wrap a 256-byte page.

`emu_fw.c` builds Felucca's `storage.c` and `cr_settings.c` on it with `FELUCCA_FLASH 1`. The rest of the UI is
still built RAM-only by `emu_firmware.h`.

| option | meaning |
| --- | --- |
| `--flash PATH` | the image file (default `build/emu/flash.bin` in windowed runs) |
| (headless) | no file unless `--flash` is given: every scripted run starts fresh and stays deterministic |
| `--no-flash` | RAM only |
| `--save-on-exit` | save the settings at exit, without waiting for the 1.5 s quiet time |

`emu_fw.c` takes these options out of `argv` before `emu.c` parses them; they become `--demo`, which ChoralRoot
ignores. The dump (F12 / `dump`) prints the flash file, the number of writes and saves, and whether the record
read as current, migrated or defaults.

`sh tools/emu/test_persist.sh` checks it end to end:

1. SELECT +17 sets 137 BPM; the script waits, quits, and the record is saved once.
2. A relaunch on the same file reads 137 BPM. The BPM meter is captured to `build/emu/test/persist_bpm.ppm`.
3. A run without `--flash` reads 120 BPM.
4. VA: a deep edit saved to U01; the relaunch powers on with U01 and its patch CRC as saved.
5. The picker's roots: KNOB 4 in the picker sets them to play; the relaunch opens the picker with them playing.
