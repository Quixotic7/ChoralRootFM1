# The flash diet (2026-10-16): what left the firmware's slot, what it cost, what was rejected

The firmware slot is fixed: XIP 573 440 B, flash 0x5000 .. 0x91000 (the boot stub and the data objects around it,
docs/BOOT-SAFETY.md). The GRAIN engine, built on its own branch (claude/grain-engine-f724f9), fills the slot to 99.3 %
when merged with main: the owner chose a diet over growing the slot ("Lets roll with the diet"). The rules of the
diet: the sound and the screens stay as they are, proven bit for bit (the 313 golden renders of `tests/regress.c`
and the engines' own tests; the 313 pixel-exact draw goldens of `tests/run_cr_draw.sh`); a table computed at run
time must not cost the audio ISR anything (the heavy pads have no headroom on the unit: docs/INTEGRATION.md
"Measured on the unit"); a table moved from flash to RAM or POOL needs room there, so shrinking is preferred to
moving; one change a commit, its size line before and after in the commit message; costs on the unit counted from
the listing (`build/choralroot.dis`, about 205 instructions a microsecond), not estimated.

The baseline: main 646abb5 (v0.23 + HISTORY 3y123; main 54467ac, v0.24-beta1, builds to the same bytes of XIP, RAM and POOL), `XIP 552 860 B of 573 440 (96.4 %), RAM 78 752 (80.1 %), POOL
239 180 (69.5 %)`.

## What was done

| HISTORY | change | XIP | RAM | POOL | audio ISR | proof |
| --- | --- | --- | --- | --- | --- | --- |
| 3y126 | FM TONE's 27 HARM waves stored as their first half (odd symmetry, exact), whole cycles built into POOL at power-on (`eng_quad.c quad_init`) | -13 728 | 0 | +27 704 | none: `quad_render` byte for byte the same (it reads SRAM now) | `cr_quad_test` 31 466 (+ the mirror check), 313 goldens |
| 3y127 | FM6's `FM6_SIN` built at power-on by Dexed's integer rotation; `FM6_MKI_LOG` / `FM6_OPL_LOG` mirrored from stored quarters (`fm6_tables_init`) | -6 512 | 0 (`.data` -> `.bss`) | 0 | none: the per-sample loops the same length | `fm6_test` (+ the checksum check), `cr_fm6_test`, 313 goldens |
| 3y128 | CZ-1's 64 factory tones packed, a bank of 16 an LZ4 block (`tools/lz4blk.py`), unpacked where a bank or a tone is read (`cz_patch.h cz_lz4_block`, `eng_cz.c cz_factory_bank`) | -4 288 | 0 | 0 | none (the unpack runs in the main loop's load paths, ~2.3 KB of its stack) | `cr_cz_test` 128 of 128 tones, `cr_backup_test`, 313 goldens |
| 3y129 | the fonts' Latin-1 extras ((c), the umlauts) nothing prints leave S, M and CRB | -6 000 | 0 | 0 | none (UI) | the 335 draw renders byte for byte the same, `text_ref_test` 0 pixels differ, `text_spacing_test`, `ui_test` |
| 3y130 | the kerning pairs grouped by their first glyph (`AF_<N>_KROW` / `_KB` / `_KD`): 2 B a pair for 3 | -1 872 | 0 | 0 | none (UI) | the same four |
| 3y131 | more of the main loop built for size (`tools/size_fns.py`: the stores, the backup, the pool, the layouts, the engines' load paths, `fm1_main`): 676 -> 824 functions minsize | -2 816 | 0 | 0 | none: 295 audio-side functions keep their instruction counts | listing against listing; the suites |
| 3y150 (after the diet, the owner's choice) | FM TONE's HARM read from the halves in flash with a mirror (`eng_quad.c quad_hw`); `quad_harm` and its power-on build (3y126's) removed | +720 | 0 | -27 704 | the HARM presets: GLASS PAD +9.4 %, DRONE +9.1 % on the host; `quad_render` 16 732 -> 17 552 B | `cr_quad_test` 31 466 (the mirror read checked at every point), 317 goldens |

## What was rejected, and why

| candidate | what it would save | why not |
| --- | --- | --- |
| HARM read with a mirror (the second half read reversed and negated a sample at a time, bit for bit the same values) | XIP -12.1 KB with nothing moved to POOL | `quad_render` +1.7 KB; the two HARM presets' host count +26 % (GLASS PAD 1209 -> 1520, DRONE 1183 -> 1503 instructions a sample): FM TONE's 8-note chords on the unit have no room for it while recording. **Taken since (3y150)**, in a leaner form (+149 / +143 instructions a sample, about +9 %), below |
| `CZ_RECIP` as two halves (x >= 1024 in 16 bits less 32 768) | XIP -1 632 (the table -2 044, the code +412) | the ramped loops of `cz_native_line_mv` grew 4 .. 6 instructions a sample (7 340 -> 7 690 B; `cz_native_noise` 554 -> 612): about 50 us a half for 8 voices with their DCW moving, for 1.6 KB |
| the value-name lists of FM TONE's RATIO / OFFSET columns (`N_QUAD_OFS` 2 KB) formatted at run time | ~2.3 KB | the F_INT-with-names path is read in `params.c`, `cr_pages.c`, `cr_edit.c` and the editor protocol's catalog (`web/test_web.mjs` compares it): too wide a change for 2 KB |
| `eng_page_t` / `param_desc_t` packed tighter (12 KB of `*_PAGES`) | ~3 KB | the struct is already 20 B a column (int16 ranges, a byte format); only `unit` could go, into a union with `names` |
| S and M with fewer phases, CRX at a smaller master size, S Huffman-coded | 17 KB, ~2 KB, 1.5 KB | pixel changes (the phases place every native label; CRX is resampled from its 104 px master); the S decode would slow the most-drawn labels |
| `cr_ring` (6.1 KB) and `CZ_CARRIER` (4 KB) built at power-on into POOL | 10 KB | POOL: with GRAIN's two 32 KB buffers and the HARM cycles it stands at 96.6 %; RAM (80 %) has no room for 10 KB either |

## The glyph audit (what the screens draw)

A throwaway build logged every glyph fetched by `gfx.c glyph` and `cr_gfx.c cr_glyph_of` (face, character) through the
335 renders of `tests/run_cr_draw.sh` and the emulator's `test_cr.sh`, `test_persist.sh` and `test_retired.sh`. What
was seen (the corpus, not a proof of what a unit may ever print: a sound's name is any of `cr_name.c CN_SET`, an
imported DX7 or Casio name any ASCII 32..126):

| face | glyphs drawn | never drawn |
| --- | --- | --- |
| S (12 px, labels) | 64: ` +-./0123456789:>A-T V X a-y` and the middle dot | `!"#$%&'()*,;<=?@UWYZ[\]^_\`qz{|}~`, the Latin-1 extras |
| M (15 px, values) | 64: ` +,-/0-9:?A-W a-y`, the ellipsis and the middle dot | `!"#$%&'()*.;<=>@JQXY[\]^_\`jqz{|}~`, the Latin-1 extras |
| CRX (104 px, the chord name) | 30 of its 37: `#+-0-9ABCDEFGIMNPTdimsu` | ` .?JWZb` (b, the flat, is drawn whenever a flat chord sounds: the corpus has none; the seven stay) |
| CRB (40 px, bold) | 80: ` #%&()*+-./0-9:>A-Z_a-z`, the ellipsis, the degree sign, the middle dot, e grave | `!"$',;<=?@[\]^\`{|}~`, the Latin-1 extras |

Dropped (3y129): the Latin-1 extras no screen prints and no name can hold. Kept: every ASCII glyph (a name can hold
it), `?` (the fallback glyph), CRX's seven.

## The account

The audio side, main's listing against the diet's: of 295 render, block, tick, ISR, mix and effects functions, one
changed its instruction count, `fm6_render` 1146 -> 1151 (3y127: the register allocation of its set-up, the
per-sample loops the same length span by span).

| | main 646abb5 | after the diet (3342d18 + 3y132) | GRAIN merged on the diet (throwaway merge of 80465ac, built, not committed) |
| --- | --- | --- | --- |
| XIP | 552 860 B (96.4 %) | 517 644 B (90.3 %): **-35 216 B** | 537 068 B (93.7 %): **36 372 B of margin** (GRAIN's own build had 3.8 KB) |
| RAM | 78 752 B (80.1 %) | 78 752 B (80.1 %) | 80 176 B (81.6 %) |
| POOL | 239 180 B (69.5 %) | 266 884 B (77.6 %) | **333 900 B (97.0 %): 10.2 KB left** |

Where the 35 KB came from: FM TONE's HARM halves 13.7 KB, FM6's tables 6.5, the fonts 7.9 (the extras 6.0, the kerning
rows 1.9), CZ-1's bank 4.3, the main loop built for size 2.8. Of the asked-for 40 KB, 35 is in; the rest of the list
(docs/FLASH.md "rejected") was measured and turned down for its cost in the ISR or in POOL / RAM.

**POOL was the next wall, and the owner decided it (3y150): HARM by mirror read.** Having played 8-note FM TONE chords
with every effect on (Geek Out 2 about 65 % on the average, peaks of 80 to 90 %): "I am fine with that trade and think
the mirror read is pretty reasonable. I'd like to keep things open for potentially more engines or FX." The reason:
POOL's 27.7 KB of HARM cycles bought XIP that the slot no longer needs as badly (36 KB spare with GRAIN), while POOL at
97.0 % left 2 KB for anything new. The read was written again, leaner than the rejected one: both halves read the same
pair of stored points (`u = (ph ^ s) >> 23`, s the phase's sign), so the slope is shared and only the start flips
(`l - ((l + h) & s)`): `quad_render` +820 B (16 732 -> 17 552; the rejected one +1.7 KB), GLASS PAD 1593 -> 1742
(+9.4 %), DRONE 1575 -> 1718 (+9.1 %) host instructions a sample, measured the same day (+149 and +143; the rejected
one added 311 and 320). Size: XIP 537 068 -> 537 788
B (93.7 -> 93.8 %), RAM 80 176 unchanged, **POOL 333 900 -> 306 196 B (97.0 -> 89.0 %): 37.0 KB left**.

What the owner weighed before (3y132): the HARM cycles (27.7 KB) in POOL buy 13.7 KB of XIP at zero ISR
cost. The two other ways out were built and measured: (a) HARM read with a mirror, bit-exact, XIP -12.1 KB, POOL
untouched, FM TONE's HARM presets +26 % of the render (rejected above); (b) HARM whole in flash as before (XIP margin
with GRAIN 22.6 KB, POOL 89.0 %). Anything else that wants POOL (a fifth P5 voice, the effects on core 1, a longer
delay line) now competes with GRAIN's two 32 KB buffers.

**Not done, by the rules**: nothing flashed, nothing pushed (the designer-handoff session holds the unit); the UI
frame's cost on the unit after 3y131 is not read (the emulator cannot see the device compiler's -Oz; the owner's
`pk_*` readout would); `tests/target_budget.txt` rewritten from the final listing (3y132: the tool's nesting heuristic
reads `fm6_render` and `quad_render` differently now, their per-sample loops the same length).

**Since: the chord layouts (0.24, HISTORY 3y163, docs/CHORD-LAYOUT.md).** Options > Chord Keys (the FM-1 layout, the
SUS Window, LOCK on LFO), Full K Shorts and MIDI Keys, against main 355fd58: XIP 537 804 -> 541 948 B (+4 144, 94.5 %:
31 492 B of margin left), RAM 80 176 -> 80 368 B (+192: MIDI Keys' queue, the waiting roots, the type keys' state),
POOL 306 196 B unchanged. The design estimated +2.5 .. 4.5 KB of XIP and about 160 B of RAM. Nothing in the audio ISR
but MIDI in's channel check (`crm_key`) and the queue's post.

## What the flash holds, and what ChoralRoot leaves alone (HISTORY 3y231, 2026-11-06)

An inventory of the 1 MiB on an FM-1, from `firmware/hal/fm1_flash_map.h`, `storage.c`, `cr_ui.c cu_flash_erase` and
docs/BOOT-SAFETY.md. "Linked" sizes are from `nm -S` on build/choralroot.elf.

| flash | what | ChoralRoot |
| --- | --- | --- |
| 0x00000..0x03FFF | the head (SPL, isd_config) | never written |
| 0x04000..0x92FFF | the stub, the app slot (body 0x5000..0x90FFF, 573 440 B), the marker's sector 0x91000 (its last 32 B), 0x92000 (cfg) | the update loader only |
| 0x93000..0x96FFF | 16 KB, the plain XIP window's first sectors (XIP 0x0208F000) | **never used** |
| 0x97000..0x9EFFF | 0.21's VA store, FM6 stores, CZ-1 tones 1..16 (before them Felucca's projects) | **free since 3y231** |
| 0x9F000 | FM6 bank copy A | live |
| 0xA0000..0xA1FFF | 0.21's CZ-1 tones 17..32 | **free since 3y231** |
| 0xA2000..0xB1FFF | the CZ-1 banks A..H | live |
| 0xB2000..0xB3FFF | 0.21's FM TONE store | **free since 3y231** |
| 0xB4000..0xB7FFF | the session, the favourites | live |
| 0xB8000..0xC7FFF | the sound pool (13 banks over 16 sectors) | live |
| 0xC8000..0xDBFFF | the ten loop slots | live |
| 0xDC000..0xDFFFF | 0.21's user banks (before them Felucca's user presets) | **free since 3y231** |
| 0xE0000..0xE4FFF | the update loader's staging | during an update only |
| 0xE5000..0xE7FFF | 12 KB (fm1-base and FIZZ keep their BLE store here; Melodee reads 0xE7000 as a possible VM) | **never used** |
| 0xE8000 | the stock VM: the radio's factory calibration (records 106, 107, 108, 187) | kept, never written (Bluetooth would read it: docs/BLE-LINK.md) |
| 0xE9000 | BTIF, the stock firmware's Bluetooth pairing | kept, never written |
| 0xEA000..0xFBFFF | the stock firmware's resources (72 KB) | kept, never written |
| 0xFC000..0xFEFFF | the settings A / B, FM6 bank copy B | live |
| 0xFF000.. | key_mac | never written |

So 28 KB were never used (0x93000..0x96FFF, 0xE5000..0xE7FFF) and 64 KB are free since 3y231 (0.21's sound objects:
0.22 .. 0.29.2 copied them into the pool once; nothing reads them now). 0x93000..0x9EFFF is one contiguous 48 KB run in
the plain XIP window: docs/BLE-LINK.md's resource region. On the SMK-37 the same retirement frees 0x9D000..0xA8FFF and
0xB0000..0xB3FFF (storage.c's SMK map); its stock VM is ChoralRoot's store there.

**Felucca's code that is still in the tree.** Nothing unused is linked: the image is a unity build, so a static
function or table nothing calls is dropped by the compiler (Felucca's factory patterns, its sequencer, the retired
engines' DSP all compile to 0 bytes). What is linked and barely used: Felucca's four-part instrument (parts 2 and 3 idle:
about 6 KB of RAM / POOL, half of `trk` and each engine's patch for them; the formats and the protocol count four
parts), the CDC serial console (about 1.2 KB of flash and 2.3 KB of RAM, shown only with USB audio off or in SAFE
MODE), and the DIGITAL -> FM6 converter (`fm4_convert.c`, ~1.8 KB). In the tree but never built into the firmware: 37
Felucca files (~15 400 lines: its UI, sequencer, editor, projects and the retired engines), still compiled by the Felucca
host tests (`tests/run_tests.sh`), `tests/hostsim.c`'s defaults and `tools/emu/test_retired.sh`.
