# The second core: the bass part on core 1 (2026-10-09), P5's chord voices (2026-10-13), the VA's (2026-10-17)

**Status: the owner's unit has run with Dual Core On since 0.16** (the readings of docs/PROPHET.md and docs/FX.md
are from it). 0.22's changes (the default, P5's chord voices on core 1) are built and host-verified, not on a unit.

**ON BY DEFAULT SINCE 0.22** (the owner, 2026-10-13: "Lets also default to using Dual Core. If users have issues,
then they can switch to 1 core."). From 0.16 to 0.21 it was **Off by default** (the user's decision, 2026-10-10:
enough users of other FM-1 firmwares report crashes with both cores running: "Reliability" below, which stands as it
was written). What the new default means, and for whom:

- **A unit with no settings record** (a new unit, erased flash data) **and Options > Reset Settings: On.**
- **A unit that has a settings record keeps what the record says.** The firmware cannot tell an Off that the player
  chose from 0.16 .. 0.21's default Off: both are bit 6 of `view` clear in a v12 .. v15 record, and nothing else in
  the record says whether the option was ever touched. So an update to 0.22 changes nothing on an existing unit:
  **whoever wants the second core turns it on in Options > Dual Core and restarts** (or uses Reset Settings). The
  release note says so. A record's Off is never turned On by the firmware (also not where a damaged view byte takes
  its default: `cr_settings.c crs_sanitize`). Records older than v12 load Off, as since 0.16.
- **If a unit has trouble with it**: Options > Dual Core Off, restart. That is the owner's fallback, and the
  fail-safes below are what stands between a unit whose second core misbehaves and that menu. A unit that crashes
  before its menu can be reached, or whose owner would rather not depend on it: **the single-core package** (next).

## Two packages: Standard and Single core (0.22, HISTORY 3y49)

The owner, 2026-10-13: "lets add option for single-core to the installer in-case users have dual-core issues."
Melodee (another FM-1 firmware) found that on some FM-1s the second core misreads shared memory and those units
crash; it ships a Standard (dual-core) and a Single core package, and its installer offers Single core under
Advanced / Hardware fallback. ChoralRoot does the same from 0.22:

- **`choralroot-X.Y.fwsc`, Standard**: as above (Dual Core an Option, On by default).
- **`choralroot-X.Y-1core.fwsc`, Single core**: the same firmware built with **`CR_SINGLE_CORE=1`**. Core 1 is
  **never started, whatever the settings say**: `cr_c1_power_on` (`cr_cpu1.h`) forces `cr_c1_off` and `cr_c1_boot`
  returns before the rails are raised, and the start (`fm1_cpu1_start`, the PC-limit opening) is compiled out. GEEK
  OUT reads `c1 off`; everything runs on the one-core rules (the shared budget of 8 voices, P5's four and a P5 bass
  one, `cr_dualcore_active()` 0). **Options > Dual Core reads "1 core build"** (the line under it "the single core
  package"); KNOB 1 changes nothing there, so the record keeps what it held for the Standard package. Reset Settings
  says no "restart". Nothing else differs: the same identity (FM-1_920), the same settings, session, favourites,
  sounds, loops and backup formats (`CR_SINGLE_CORE` appears only in `cr_cpu1.h`, `cr_ui.c`, `cr_ui.h`).
- **The version** reads **`ChoralRoot X.Y 1-core`** on the splash, in Options > Version and over SysEx (INFO: what
  `fm1_install.py --info`, the installer and the device manager read). A word after a space, so every parser that
  reads the first word after "ChoralRoot" (the installer's `unitVersion`, `fm1ota.js classifyFirmware`,
  `fm1backup.js backupFamily`, `fm1_install.py classify_firmware` / `family`, `cz_pop_check.py`) sees the version
  and the family unchanged; the installer shows it as "ChoralRoot X.Y (single core)".

**When to use it**: an FM-1 that crashes, freezes or restarts with Standard (with Dual Core On), especially one that
does so before Options can be reached. The installer offers it under **Advanced > Hardware fallback** for a release
that lists it (`web/releases.json` `sha256_1core`; 0.16 .. 0.21 have none), with Standard the default; when the unit
already runs the single-core package, "What is on my FM-1?" says so and Install offers Standard back unless Single
core is chosen. **Moving between the two keeps everything**: both read and write the same flash records
(`tools/emu/test_persist.sh` "the two packages": the settings and the favourites written by one are read by the
other; a record that says Dual Core On is still On after the single-core package ran on it; `web/test_manager.mjs`:
a backup taken on one restores on the other).

**Building it**: `CR_SINGLE_CORE=1 ./build.sh` (the version gets " 1-core"; the size line reads the same as the
standard build's but `.bss` is about 3 KB smaller); `./build.sh --release X.Y` builds both (BUILDING.md
"Releasing"). The emulator: `CR_SINGLE_CORE=1 sh tools/emu/build.sh` -> `build/host/emu_1core`.

**What core 1 runs** (0.22): the bass part's render, as since 0.15, and, with P5 on the chord part, a share of the
chord part's voices: at most four P5 voices a core, eight on the chord part (docs/PROPHET.md "Voices", "Two cores,
eight voices"). With Dual Core Off, or core 1 not running, ChoralRoot runs on one core, as the stock firmware does,
with the shared budget of 8 voices (P5: four, and a P5 bass one).

The FM-1's chip (JieLi AC79 / WL82) has two pi32v2 cores. With Dual Core On, ChoralRoot starts the second one at
power-on and, in every audio block, hands it the BASS part's render (part 1) while core 0 renders the CHORD part
(part 0). With core 1 alive each of the two parts has its own voice budget: 8 voices (16 units: FM6 16) for the chord
part and the looper, 8 for the bass part. If core 1 does not answer, ChoralRoot runs on one core, with the shared
budget of 8 voices, as it does with Dual Core Off.

## The bring-up (`firmware/hal/fm1_cpu1.{h,S}`)

Copied from fm1-x0x (GPL-3.0), which runs it on the device, after Melodee's dual-core audio (GPL-3.0, Kerem Kilic)
and JieLi's AC79 SDK `EnableOtherCpu` (Apache-2.0). The registers, as ported:

| What | Where |
| --- | --- |
| core 1's entry address | the word at `0x01C7FFF8` (top of RAM, write-protected later by `fm1_guard_lock_top`) |
| its interrupt bank, all off | `0x1EEF300..0x1EEF37C` (core 0's is at `0x1EEF100`) |
| held during the start | bit 3 of `0x10008` (clock / power), restored after |
| C1_CON | `0x1EEE004`: start = bit 3 set (enabled), bit 1 cleared (out of hold); hold = bit 1 set, then bit 3 cleared |
| the clock it is timed with | TIMER4 (`fm1_ticks`, 24 MHz) |

`fm1_c1_entry` (`.S`, in the flash image's `.text`): `cli`, a breadcrumb in `fm1_c1_trace`, `icfg = 0`, `usp` = its own
2816-byte stack (`_c1_ustack`, .bss; after `rti` the core runs on usp), `sp` / `ssp` = a 256-byte exception stack,
`reti = fm1_c1_main`, `rti`. `fm1_c1_main` lives in `.c1_text` (RAMTEXT, right after `.ram_text`; `app.ld` sets
`_rt_end = _c1_end`, so main.c's one copy loop and the guard's RAM code window cover both, and `tools/build.py` puts
`.c1_text` in the image): it reports in (`fm1_c1_alive`), then loops on the job counter forever, from RAM.
(Since 3y41 the `.ram_text` output section can also hold audio functions, `.rhot.*`, in a build with a `CR_RAM_*`
switch: `.c1_text` then starts that much higher, still inside the one copy and the one PC window. With
`CR_RAM_CORE` core 1's job, `c1_part1_job`, and `track_render_x` run from RAM on both cores: docs/FX.md "Code in
RAM". Not run on a unit.)

Start (Dual Core On only; `cr_cpu1.h cr_c1_boot`, from `main.c fm1_main`, after the sounds and **before `audio_init`
and the timers**:
fm1-x0x found that started later it never reaches its entry): the top of RAM unlocked, the PC limits opened
(`fm1_guard_pc_open`: core 1 boots through the chip's ROM, which they refuse; Melodee's finding), `fm1_cpu1_start`
(the stack filled with "C1MK", the entry word, the start sequence, then up to 20 ms for `fm1_c1_alive`), the PC limits
armed again (`fm1_guard_enable(FM1_GUARD_PC)`). No answer: core 1 held, `cr_dbg.c1 = failed`. In SAFE MODE core 1 is
never started (`c1 off`), as with Dual Core Off. The boot breadcrumb `BS_CPU1` ("core 1") is set first, so a hang
there shows in the boot guard's `prev_stage` and counts as a failed boot.

Held before every reset and loader entry (`firmware/hal/fm1_sys.h`): `fm1_reboot` (the crash screen, Options > Flash
Data, CR_REBOOT), `fm1_enter_uboot` (USB SysEx, the boot guard's UBOOT; until 0.17 OCT- + OCT+ 5 s, since 0.18 the update gesture on a
unit without a boot stub), `fm1_core_reset` (and so `fm1_enter_update`: the update loader, and `fm1_stub_request`: SysEx
77 STUB and, since 0.18, the update gesture OCT- + OCT+ + HOME 5 s). The loader (`firmware/loader`) never starts it.

## Memory (the size line of `./build.sh`)

`size: .text 414120 B, .ram_text 2888 B, .c1_text 60 B (RAMTEXT 2948 of 24576), .data 12072 B, .bss 72104 B;
XIP 429140 B of 581564 (73.8%), RAM 84176 B of 98304 (85.6%), POOL 327000 B of 344064 (95.0%)`

Added: RAMTEXT 60 B (`.c1_text`); RAM ~3.9 KB (core 1's stacks 2816 + 256 B, FM6's second scratch 384 B, the
matrix per part ~150 B, `cr_dbg` ~200 B, the job's state); POOL 256 B (the bass part's block and stereo side,
`c1_buf` / `c1_side`); XIP ~3 KB. 3y46 added `p5x_buf` (128 B of POOL) and the list; 3y177 `p5x_side` (128 B of POOL),
424 B of XIP and, with `CR_RAM_VA` on, 9048 B of RAMTEXT (16 120 of 24 576); 3y187 (FM6) 944 B of XIP.

## The job protocol (`fx.c mix_parts_split`, `cr_cpu1.h`)

One job per CTL-sample block (4 per half buffer), a function and an argument; single writer each way
(`fm1_c1_job` core 0, `fm1_c1_done` core 1), `csync` around each flag (the cores share the caches); no spinlocks.
In `mix_block`, after the engine tick and the events (`cr_out.c` and `events_block`: all notes, part 1's voice
allocation, stealing and the budget bookkeeping are done on core 0, before the hand-off):

1. core 0: the matrix (`mod_begin`) and the LFO tick of part 0, then of part 1 (their draws from the shared random
   generator stay in one core's order; the LFO value before the tick is kept for the render);
2. core 0 hands part 1's render to core 1 (`cr_c1_split = 1`, `cr_c1_run(c1_part1_job)`): `track_render_x(trk[1])`
   into `c1_buf` / `c1_side`;
3. core 0 renders part 0 into `part_buf`, then waits (`fm1_cpu1_wait`, 8 ms at most);
4. core 0 mixes part 0, part 1, then parts 2..3 (silent in ChoralRoot), the FX buses and the master, in the order of
   one core: **the samples are one core's, bit for bit**.

What core 1 touches: `trk[1]` (its voices, its LFO value, its patch, P_E0..7 during an engine fade, its bend), its
engine's state (`eng_state[1]`, the engines' per-part arrays, FM6's second scratch `fm6_scr[1]`), its matrix
(`mod_tab[1]`, per part since this change), `c1_buf` / `c1_side`. Not the event queues (`cr_evq`, `cr_min_q`), the
scope, the FX buffers, the looper or the UI. Shared scratch found and split: FM6's operator buses (`fm6_core.c`),
the modulation matrix's state (`mod.c`). Blocks that stay on core 0 alone (`c1_split_ok`): PHYS on part 0 (its render
draws from the shared generator on a model change) and VOICE on both parts (one breath-noise generator): neither
engine is in ChoralRoot's image since 0.18 (`FELUCCA_PHYS` / `FELUCCA_VOICE` 0), so every block splits.

What runs where (0.22: with P5 on the chord part, part 0's render is shared: core 1's job is part 1's render and a
share of part 0's voices, and core 0 renders the rest of them before it waits; docs/PROPHET.md "How a block is
rendered"):

| core 0 (the audio ISR) | core 1 |
| --- | --- |
| the engine tick, the looper, MIDI, events, voice allocation for both parts | waits in RAM |
| parts' matrix + LFO tick | |
| part 0's render | part 1's render (`track_render` of trk[1]) |
| waits for core 1 | waits in RAM |
| DIST, level, pan, sends of every part; parts 2..3; FX buses; master; USB tap; click | |

### What each core runs (3y50: the reverb on core 1)

Since 3y50 a block has **two** jobs for core 1 when it is alive (`cr_dbg.c1` C1_OK), both handed over and waited for
in a fixed order on core 0:

| core 0 (the audio ISR) | core 1 |
| --- | --- |
| the tick, events, voice allocation; both parts' matrix + LFO tick | idle |
| (P5 or VA chord) its share of the chord's voices; (else) part 0's render | **job 1**: part 1's render (+ its share of the P5 or VA chord's voices) |
| join 1 (`CRP_WAIT`) | idle |
| DIST, level, pan, sends of parts 0 and 1 (the sends are whole from here) | idle |
| `xf_prep` (the coefficients, what runs), then hands over job 2 | |
| the chorus (`xc_block`), the delay (`xf_delay`) into the mix | **job 2**: the reverb (`xf_reverb`: its half-band, its eight lines, the wet's up-sampler) into `c1x_l` / `c1x_r` |
| join 2 (`CRP_WAIT` and `fxw`), `mix += c1x` | idle |
| the master, the USB tap, the click | |

Why the reverb: the effects need the sum of both parts' sends, which exists only after join 1, so they cannot run
under the voices without a block of latency (not bit-exact, not taken); what can overlap is one effect with the
others. The reverb is the largest (the unit: avg 202 / max 273 us a half, plus its down- and up-sampler 22..98 each)
and owns state no other effect touches (`xf.r_*`, `xf.dn[1]`, `xf.up`, `xf.t[1..3]`, `xf.tr`, `xf.tx`, `xr_buf`,
`xr_pbuf`); the chorus (`fx.cho_*`, `xc`, `cho_buf`) and the delay (`xf.d_*`, `xd_buf`, `xf.tf`) stay on core 0.
Every effect ADDS its wet into the mix (`+=`, integers, no saturation between): the reverb into buffers of its own,
added by core 0 after the join, gives the mix that the serial order gives, bit for bit, whatever runs first (proof:
`tests/cr_c1_thread_test.c` "effects": inline = thread = the effects all on core 0 (`cr_c1_test_fx0`), every pair of
engines, three sends up; ThreadSanitizer clean; test_cr.sh `cr_prophet_eight_fx`: inline and `EMU_C1=thread` WAVs
equal). A block with nothing for the reverb (no send, its tail and the up-sampler's over) hands nothing over.
**The delay at the half rate** (`CR_FX_DELAY_DS 2`, the default since 0.22: HISTORY 3y74) writes its wet into the
reverb's half-rate buffer (`xf.t[2..3]`), and one up-sampler serves both. When the two branches met (3y76) that would
have kept the whole block on core 0, as this paragraph first said; now (`XF_SPLIT_HALF`) core 1's job is the reverb's
half-band and lines alone, into half-rate buffers of its own (`c1h_l` / `c1h_r`, 2 x 64 B of POOL; `xf_reverb` with
`up` 0), and core 0 runs the delay into `xf.t[2..3]`, adds core 1's after the join and up-samples the sum
(`xf_reverb` with `rev` 0: the up-sampler alone, about 35 us of a half on core 0). The up-sampler's tail alone, with
no lines to run, is no job. Integer sums again: the mix is `xf_block`'s, bit for bit (`tests/cr_c1_thread_test.c`
"effects" at the default build). The delay at the half rate with the reverb at the full one (a build switch only)
keeps the block on core 0 (`XF_SPLIT` 0). Dual Core Off, core 1 failed or given up: `xf_block` on core 0, the three
pieces in a row, as before.
Job 2 refused, stalled or faulted: core 1 held for good (C1_GAVE_UP) and core 0 runs the reverb itself in that block,
as it reruns job 1.

**The split of the chord's voices** (3y46's rule for P5, kept in 3y50; weighed per engine and opened to the VA in
3y177, below): core 1 takes the k voices that make the longer of the two sides (core 1: the bass part + k voices;
core 0: the rest) shortest, at most four a core for P5, a tie to core 1. 3y50 looked at weighing them (the survey's
item 2): every chord voice plays the part's one program, so they all weigh the same, and the bass part is already
weighed (P5 voices as voices, another engine by its units); core 0's own fixed work (the tick, the sends, its
effects, the master) lies **outside** the window in which the voices overlap (before the hand-over or after join 1),
so it cannot be balanced against voices. With the measured costs the weighted rule gives today's 4 + 4 for 8, 3 + 3
for 6, 2 + 2 for 4 (an odd one to core 1); it was not changed then.

### The VA's chord on both cores (3y177)

The unit's heaviest case was a VA chord on core 0 alone: MORPH PAD, 8 notes, over PUNCH BASS, part0 2293 us and a
block of 3121 of 2902 (108 %, 26 voices shed in 20 s), while core 1 waited after its 355 us bass (docs/INTEGRATION.md
"Measured on the unit"); on 0.25 (2026-10-17, the reading before this change) part0 2397, block 3256 (112 %), late
36, while core 1's bass took 375. The review of Melodee's dual-core scheduling (docs/notes/melodee-p5-dualcore-review.md, item
1) found this the largest gain available. What changed, and what did not:

- **Which engines split**: `engine_t.split`, a voice's weight in 10 us (P5 44, the VA 28; 0 = the part renders whole
  on core 0), replaces `P5_IS(trk[0])` in `mix_parts_split`. The list (`voice.c` `p5x`), the hand-over, the job and the
  fallback are 3y46's: core 0 prepares every voice before the ONE hand-over, `c1_part1_job` renders the bass part and
  then core 1's share of the list, core 0 renders the rest, waits, adds. Core 1 not answering, refusing or given up:
  core 0 runs the same job itself, in this block ("Reliability" and "The persistent fallback" are unchanged).
  Melodee's streaming queue is not taken.
- **The side**: the VA renders a stereo side (`render2`: SPREAD, USPREAD). Core 1's voices add theirs into
  `p5x_side` (128 B of POOL, beside `p5x_buf`) and say so in `p5x_side_on`; core 0 adds it into the part's side after
  the wait, as it adds `p5x_buf` into the mid, and the part is stereo when either core's voices made a side. Integer
  sums again: the part is the unsplit render's, bit for bit.
- **The weights** (`fx.c p5x_assign`): a chord voice weighs its engine's `split`, the bass as before (a P5 voice 440,
  another engine 75 a budget unit), and **core 0's side x 1.07** (`P5X_C0_PCT`): TIMER5 nests in core 0's audio ISR,
  about 7 % of it dry (Melodee's "CPU0 service allowance", the idea alone). The four a core stay P5's. For the VA chord
  (core 0 + core 1) over a sounding MONO bass: 8 voices 4 + 4, 7 4 + 3, 6 3 + 3, 5 3 + 2; with no bass note the odd
  voice goes to core 1 (7: 3 + 4); over a P5 stack of four (1760 us on core 1): 8 voices 7 + 1. For P5 the factor changes no split with a MONO or a P5 bass (the review's
  simulation, again here); it does with a POLY bass of another engine of three voices, or of six or seven voices of 1
  unit (one more voice to core 1 for a 2-, 4- or 6-voice chord): the samples are the same, only the balance moves.
- **What a VA voice's render touches** (`eng_va.c va_render`; the audit, docs/VA.md "Two cores"): its own
  `va_vs[part][voice]` and `voice_t` (phases, filter, noise seeds, ramps), the buffers it is given, and, from the
  latest note's voice only, the part's `va_pan_off` / `va_rate_off` (one writer, read on core 0 after the join).
  It reads the part's `va_lfo` and patch, written by `va_block` on core 0 before the hand-over.
- **Core 1's stack**: `c1_part1_job` (68 B) -> `va_render` (844 B) is shallower than the path a VA bass already took
  (`c1_part1_job` -> `track_render_x` 212 -> `va_render`), and than FM TONE's (`quad_render` 856 under
  `track_render_x`): nothing new for the 2816 B.
- **The flash**: with both cores running `va_render` from XIP they share the cache. `CR_RAM_VA` puts it in RAMTEXT
  (16 120 of 24 576 with it; `ram_code.py cmp`: 4 moved, 0 differ, veneers `memset` and, since GRAIN shares the
  filter's weights, `va_fweights`, which the switch had lacked and failed to link without), **on in a device build
  since 3y177**: on the unit the split alone took MORPH PAD's worst half from 3263 to 2389 us, and the VA in RAM to
  2103, with no late half and no shed dry or recording (docs/VA.md "Two cores").

Proof on the host: `tests/cr_c1_thread_test.c` (inline = thread = unsplit = all to core 1 = none to core 1, every
sample, for six VA-chord pairs: a VA, P5, FM6, CZ-1 bass, MORPH PAD over PUNCH BASS with its SPREAD side on core 1,
a UNISON chord with USPREAD; ThreadSanitizer clean), test_cr.sh `cr_va_eight` (eight MORPH PAD voices and the bass:
inline = `EMU_C1=thread` bit for bit, core 1's average job about core 0's; `EMU_C1=nostart` seven under the bass, one
core's budget; `EMU_C1=hang` one core's WAV, Dual Core Off). The goldens did not change.

### FM6's chord on both cores (3y187)

`ENG_FM6.split` 20 (a voice 200 us, the unit's TINE EP). The audit (docs/FM6.md "Two cores") found two shared
things in a voice's render, both fixed without new RAM: **the render's scratch is now the core's, not the part's**
(`CR_C1_IDX`: `CR_C1_SELF()`, the `cnum` register; core 1 renders the bass and its chord voices one after the other
with its one `fm6_scr`, so the third scratch the review expected, 384 B, is not needed), and **the part's AMS shares
are figured in `fm6_block`** on core 0 before the hand-over, so the voices only read them. ThreadSanitizer without
either: 10 / 4 reports; with both: none. XIP +944 B (94.9 %), RAM and POOL +0. Proof: `tests/cr_c1_thread_test.c`
six FM6-chord pairs (an FM6, VA, P5, CZ-1 bass; TINE EP over DEEP SUB; a patch with AMS on every operator and the
LFO's AMD: its shares in use in 2948 of 3000 blocks), test_cr.sh `cr_fm6_eight` (inline = `EMU_C1=thread` =
`EMU_C1=nostart`, one core's WAV, bit for bit); the goldens, one core and two, unchanged.

**FM TONE's chord (3y206, 2026-10-31)**: `ENG_QUAD.split` 20 (`QUAD_VOICE_US` 200: WOBBLE's 8 voices part0 1509 us on
the unit, ~190 a voice, DRIVE PRE ~+20). The audit (docs/QUAD.md "Two cores"): a voice's render writes its own
`quad_vs[part][i]` and the buffers it is given; the part's LFO phases are `quad_block`'s (core 0, before the hand-over),
the voices read them; the latest note's voice alone writes `quad_pan_off` / `quad_fx_off` (fx.c reads them after the
join). One shared read: DRIVE PRE (3y205) took the LFOs' DRIVE from `quad_fx_off`, which the latest voice may be
writing on the other core in the same block: now `quad_dro`, the last block's value, kept by `quad_block`. Proof that
it was needed: with the old read the DRIVE-PRE pair's thread render differs from the inline one. Proof:
`tests/cr_c1_thread_test.c` FM TONE chord pairs (WOBBLE over a VA bass; DRIVE 80 PRE with LFO 1 on DRIVE over an FM
TONE bass; the old FM TONE / FM TONE pair now split): inline = thread = unsplit = all to core 1 = none, ThreadSanitizer
0 reports; test_cr.sh `cr_quad_eight` (eight EP voices DRIVE 127 PRE over DEEP SUB: inline = `EMU_C1=thread` bit for
bit). One core (`EMU_C1=nostart`) is not the same WAV: eight FM TONE voices are the whole 16-unit budget, so the bass
note takes one (7 + 1), as before this change. GRAIN would still need its audit first
(docs/notes/melodee-p5-dualcore-review.md "The other engines"); CZ-1's chord stays on core 0.

Publishing job 1 before core 0 has prepared all of the chord's voices (Melodee's per-job publishing) needs a mailbox
deeper than `fm1_c1_post`'s one job (core 1's voices are prepared by core 0 in `track_render_x`): not done.

The shed (`audio.c shed_check`) times core 0's whole half, both joins' waits in it: the figure that misses the
deadline. 78 PEAK 55..61 (docs/FX.md "Each core's half") reads each core's busy time apart.

The wait is inside the half's time: the CPU meter and the overload shed (`audio.c shed_check`) see the true block.
Core 1 only works inside the audio ISR, which waits for it before it returns; core 0 turns the flash off (saves,
updates) only with interrupts off; idle, core 1 runs from RAM. So the flash is never off under it.

## The budgets (`voice.c voices_busy_of`, `voice_victim`)

With core 1 alive (`cr_dualcore_active()`): part 1 has its own VBUDGET (16 units) and part 0 with parts 2..3 another;
`voice_room` / `voice_alloc` count and steal only within the part's own budget. The chord part's budget is shared with
the looper's loop voices, which play on part 0. The bass part is MONO in ChoralRoot (voice mode 1 whatever the preset),
so its budget matters for MIDI notes and the loop on the BASS channel. With core 1 off (the default: Options > Dual
Core Off), failed or given up: the shared 16 units of all parts, as before (Felucca, `tests/regress.c`: `CR_CPU1 0`).

CPU: on one core (the emulator's inline job) two full budgets could reach about twice the old worst block, but only
if both parts played 8 voices; with the bass MONO the extra over the old budget is the chord part's 8th voice and the
bass's own. perf.sh (j) (an 8-note VA 13th + the VA bass, 9 voices, one more than one core's budget), Dual Core On:
740 / 1218 us of 2902 on one core, of which core 1 takes the bass's 46 / 125; Dual Core Off (the default: one of the
9 voices given up): 682 / 1130.

## The stage profile (`cr_dbg`, GEEK OUT, console `cpu`, perf.sh)

`cr_cpu1.h` marks the ISR's stages per half buffer: the engine tick, part 0, part 1 (core 1's job time when it renders
it), the rest of the mix with the FX, the master with the click, the wait for core 1. Average and maximum of the last
second (`cpu_window`): the console's `cpu` (`stage_<name>_avg_us` / `_max_us`, `c1_state`, `c1_jobs`,
`c1_stack_bytes`, `c1_start_us`) and GEEK OUT's fifth line (`t107 c443 b135 f730 m159 w102`: tick, chord, bass, fx,
master, wait, us per half). GEEK OUT's fourth line: `c1 ok <jobs> · stk <bytes>` (the stack's high-water mark from
the C1MK fill), `c1 off`, `c1 failed`, `c1 gave up`. On the device the stages count TIMER4 ticks; on the emulator
host instructions (EMU_STAGES=1; the counter's own cost measured and left out; device us = instructions / 259).

perf.sh, 2026-10-09 (Dual Core On; device estimate from the emulator, us per 2902 us half, avg / max; one core's block):

| scenario | block (one core) | tick | part0 (chord) | part1 (bass) | fx | master |
| --- | --- | --- | --- | --- | --- | --- |
| (c) FM PAD chord + DEEP SUB + loop + arp | 814 / 1048 | 7 / 93 | 507 / 781 | 58 / 169 | 142 / 418 | 58 / 127 |
| (i) (c) on FM TONE STRINGS | 840 / 1075 | 7 / 105 | 534 / 880 | 58 / 177 | 141 / 325 | 58 / 149 |
| (j) 8-note VA 13th + VA bass (MIDI) | 740 / 1218 | 6 / 57 | 461 / 865 | 46 / 125 | 126 / 259 | 58 / 85 |

perf.sh, 2026-10-10 (0.16: it runs every scenario with Dual Core Off, the default, then again with it On; the block,
device estimate avg / max us of 2902, and the voices given up over the run):

| scenario | Off (the default): block | given up | On: block | given up | the WAV |
| --- | --- | --- | --- | --- | --- |
| (c) FM PAD chord + DEEP SUB + loop + arp | 849 / 1031 | 0 | 850 / 1032 | 0 | the same, bit for bit |
| (e) (c) + the EDIT knobs | 872 / 1031 | 0 | 873 / 1050 | 0 | the same |
| (f) 6-note VA chord + VA bass, a chord change | 739 / 1126 | 2 | 753 / 1195 | 1 | differs |
| (g) 6-note CZ-1 chord + CZ-1 bass, a chord change | 820 / 1253 | 2 | 839 / 1424 | 1 | differs |
| (i) (c) on FM TONE STRINGS | 750 / 946 | 65 | 813 / 1003 | 51 | differs |
| (j) 8-note VA 13th + VA bass (MIDI) | 682 / 1130 | 1 | 740 / 1218 | 0 | differs |

(a), (b), (d), (h): the same WAV and the same block within 1 us on average. Where the WAVs differ it is the budget, not
the rendering: with one shared budget of 8 voices the bass's voice counts against the chord's (in (f), (g) and (i) a
releasing chord voice is given up a little sooner; in (j) the 9th voice is given up). One core's worst half stays
under half the budget (43 % in (g)); on the emulator the bass job runs inline, so "On" there is still one core's block.

What this says: ChoralRoot's bass part is MONO, so core 1's job is small (~50-70 us a half on average, up to ~260 in
(g)); the chord part is the big stage and stays on core 0. The win today is the budget (an 8-note chord and the bass
both whole), not CPU: about 5-8 % of a half. A split that also moves part of the chord part (or the FX) to core 1
would be the next step if the CPU is ever short.

On the device with core 1 the half is about the block less part1, plus the wait.

### Geek Out 2 (0.22): the loads and the memory as meters

The sixth view (`V_GEEK2`, stored as view 5; after Scope in HOME's cycle, the view menu and Options > View): the
editor's four tall level bars (`CR_K_EDIT8`, `tall` 2: the readout variant, no knob underlines, a peak tick) in the
knob colours. **CORE 0** (blue): the audio ISR's share of core 0 over the last second, `cpu_last.avg_us` less the
wait for core 1 (`cr_dbg.avg_us[CRP_WAIT]`, with core 1 alive) over the 2902 us half; the tick its peak (`max_us`
less the wait's; both joins' waits on this line, 3y50). **CORE 1** (orange): core 1's busy time over the same second
(`cr_dbg.c1_avg_us`, the half's `c1_busy_cur` summed over the stages' window: its voices' job, the bass part and its
share of a P5 chord's voices, 3y46, and the reverb's job where it runs there, 3y50; on the branch that made the view
it was `avg_us[CRP_PART1]`, the voices alone) over the half while `c1 ok`, `c1_max_us` the tick; `off` / `failed` /
`gave up` in the grey otherwise. **RAM** (white) and
**POOL** (green): the link-time use of the two regions (`_bss_end` over the 96 KiB RAM, `_pool_end - _pool_start`
over POOL's 0x54000): the same figures as `./build.sh`'s size line, on the unit. The shown load glides toward each
second's figure (a quarter of the way per UI frame); the ticks jump. The title line: `GEEK OUT 2`, and `2 cores` or
`1 core` on the right with the battery. The main loop's own share of core 0 (the UI, USB, MIDI) is not in CORE 0: the
meter is the engine's, as GEEK OUT's `isr` figure is. Emulator: `tools/emu/scripts/cr_geek2.txt`
(`build/emu/test/cr_geek2.ppm`, `test_cr.sh`).

## Options > Dual Core (0.15; Off by default 0.16 .. 0.21, On since 0.22)

**0.22: the default is On** (the top of this page). The rest of this section is 0.16's, when it became Off:

Options > **Dual Core: On / Off** (after USB Level; settings v12, bit 6 of `view` set = On, docs/SETTINGS.md),
**default Off** (0.16 .. 0.21). Why: other FM-1 firmwares' dual-core support is reported to crash for enough of their users
(Melodee's investigation, "Reliability" below: the margin varies per chip) that one core, what the stock firmware runs
and every unit was validated with, is the default; the second core is an option the user turns on. 0.15 had it On by
default (settings v11, the bit set = Off).

`cr_settings_boot` (persist_boot) reads it into `cr_c1_off` (1 until a record says On) before `cr_c1_boot`. Off: core
1 is never started, the core rails stay as the boot loader left them, `cr_dbg.c1` stays `C1_OFF` (GEEK OUT `c1 off`,
console `c1_state 0`), one core with the shared 16-unit budget (an 8-note chord + bass gives up a voice). On: the
rails go up, core 1 starts, the bass part has its own budget. A change in Options takes effect at the **next
power-on** (the value reads "On · restart" / "Off · restart", the line under it "takes effect after a restart";
otherwise that line reads "On: bass on the second core" when Off and "bass on the second core" when On): core 1 is
never started or stopped live.

**Updating from 0.14 / 0.15**: every settings record older than v12 loads as **Off**, whatever its bit says (a v11
record's On cannot be told from 0.15's default), and so does a backup made on 0.14 / 0.15 restored on 0.16 (the
restore runs the same import). Whoever used it turns it on again in Options. A fresh unit, erased flash data, SAFE
MODE's defaults and a Felucca / Melodee settings record: Off.

Test: `tests/cr_settings_test.c t_dualcore_v12`, `tests/cr_backup_test.c` (a v11 / v12 settings object restored);
`tools/emu/test_cr.sh`: a fresh flash (`cr_dualcore.txt`: `c1 off`, one voice given up, the WAV of a core 1 that did
not start; `cr_dualcore_on.txt`: the value "Off", then "On · restart"), then on that flash, relaunched: `c1 ok`, none
given up; `cr_dualcore_opt.txt` (back Off), a relaunch: `c1 off`; On again, a relaunch: `c1 ok`.

## Two threads on the host (0.22, HISTORY 3y45)

The host's stub core runs core 1's job inline (`cr_cpu1.h cr_c1_run`), so the emulator and the tests could show
that the split's samples are one core's, but never two cores writing one thing. Since 3y45 the stub can run the job
on a **real second thread** from the hand-over to the wait, while the first thread renders part 0, as the unit's
two cores do (`cr_c1_test_thread`; a mutex and a condition at the two ends only, nothing between):

- **the emulator**: `EMU_C1=thread` on a flash whose settings say Dual Core On. `tools/emu/test_cr.sh` plays
  `cr_dualcore.txt` (the VA pair) and `perf_p.txt` (P5 on the chord and on the bass part) that way: the WAV must be
  the inline job's. Not in the browser emulator (no threads there).
- **the test**: `tests/run_c1_thread_test.sh` (`tests/cr_c1_thread_test.c`, with all 200 P5 programs). Twelve pairs
  of engines on the two parts (P5 / P5 with a UNISON bass, P5 with FM6 and with the VA either way round, FM6 / FM6,
  VA / VA, CZ-1 / CZ-1, FM TONE / FM TONE, FM6 / VA, CZ-1 / FM TONE), a phrase of 3000 blocks with five- and
  six-note chords (steals, fades, deferred notes), bass notes, the wheel, pressure, a bend and a sound change under
  held notes: (1) the output with the job on a thread is the inline job's, every sample, twice; (2) the same built
  with **`-fsanitize=thread`: no report**; (3) with a word planted that both parts' P5 renders write
  (`-DC1_PLANT`), the sanitizer reports that word: the proof that (2) can fail. A compiler without the sanitizer
  skips (2) and (3) and says so.

**Result (3y45): no word is shared between the two parts' renders**, for P5 on both parts and for every engine
that ran on core 1 before it; the rule of this page ("what core 1 touches") stands and no engine had to be changed.
The list of P5's state and whose it is: docs/PROPHET.md "Two cores".

What this is not: a model of the unit's timing, of its caches, of a core that reads a word wrong ("Reliability"
below), of an interrupt on core 0 during the job, or of the main loop writing a parameter under both (on the unit it does
not run during the audio ISR).

**The budget makes the two settings sound different with a UNISON bass** (docs/PROPHET.md "Two cores"): with core 1
the bass part's own 16 units carry a P5 stack of four voices under a chord; with one core the stack gives way to
one. And the overload shed sees part 1's time in the half only with one core.

## The fail-safes

- **The start, with On the default (0.22; `cr_cpu1.h cr_c1_power_on`, `main.c`)**. What can go wrong at power-on and
  what the unit then does:
  - core 1 does not answer in 20 ms: held, `c1 failed`, one core for the session (as before). Nothing is saved: the
    option still reads On and the next power-on tries again;
  - **the start hangs or crashes the boot** (the breadcrumb is `BS_CPU1`, "core 1"; the watchdog or the crash's
    reboot resets the chip within 30 s of the boot, so the boot guard counts it: `cr_bootguard.h`). **The boot that
    follows a counted failure does not start core 1** (whatever the failed boot's stage was): it comes up on one
    core, `c1 failed`, Options > Dual Core reads `On · restart`, the rails as the boot loader left them, and it loads
    and saves as any boot. The player can switch Dual Core Off there, and it is saved. **This rule switches the
    option off by itself in no case** (a crash or a hang beside a core 1 that started and answered is the persistent
    fallback's, below, which does save Off: `fm1_c1_bar.ran` is set only once core 1 answered, so a start that hangs
    leaves no bar): the next power-on (which clears the guard) starts the core again, and a unit whose
    second core always fails the start so pays one failed boot (a few seconds) at every power-on until Dual Core is
    turned Off. Without this rule such a unit would fail twice at every power-on and come up in SAFE MODE, where
    core 1 is not started but nothing is loaded or saved: with On the default it could not have been switched Off
    (and erasing the flash data would have given it On again). **It cannot boot-loop**: one failed boot, then a boot
    without the core; and should that one fail too, SAFE MODE (two in a row), then the ROM's USB boot (four), as for
    any crash;
  - SAFE MODE: core 1 is never started, as before (the option then reads `On · restart`).
  Tested on the host (`tests/cr_cpu1_test.c`: SAFE MODE, the boot before failed with Dual Core On and Off, a clean
  boot); and in the emulator (`tools/emu/test_cr.sh`: a fresh unit with `--boot-fail 0 --reset-reason wdt`: a normal boot,
  `counted 1`, `c1 failed`, one core's budget). **Not tested on a unit**:
  a real hang in the start.
- Core 1 does not answer in 20 ms at power-on: held, `c1 failed`, one core for the session, the shared budget.
- A job not done in 8 ms (`fm1_cpu1_wait`): core 1 held for good, core 0 renders part 1 itself in that block (late,
  but the samples are one core's), `c1 gave up`, the shared budget from then on (the next allocations steal back to
  it). A hang in core 1 never hangs core 0. **With P5's chord voices on core 1 (0.22)** the job core 0 then runs
  itself is the same one (the bass part and core 1's share of the chord's voices), and the next block has one
  core's voice counts: `voice.c p5_pool_enforce` gives up what is above them (docs/PROPHET.md "Voices"; tested for
  a stall, a fault and a second misread under a chord of eight).
- A fault about core 1 (it took the exception itself, or the debug unit raised core 1's PC limit, write limit or bus
  error on core 0) no longer resets the chip: core 1 is held, core 0 resumes, `c1 gave up` (0.16: "Reliability").
  Before 0.16 it ended in the crash report and a reset, as any crash.
- A crash of core 0 while core 1 runs, a watchdog reset while it ran, or a give-up: the next boot keeps core 1 off
  and saves Options > Dual Core Off, with a message and the reason under the option (0.22: "The persistent
  fallback"). Before 0.22 the saved On started core 1 again at every reboot: the crash loop of ChoralRootFM1#9.
- Host tests: `tests/cr_cpu1_test.c` (the split bit for bit for 3 x 3 preset pairs, the job before and after part
  0; 8 + 8 voices with core 1, 8 without; a job that never answers; the default: never started; no answer at start).
  The emulator, on a flash with Dual Core On: `EMU_C1=nostart` / `EMU_C1=hang` (`tools/emu/test_cr.sh`,
  `cr_dualcore.txt`), `EMU_STAGES=1`.

## Reliability (0.16, after Melodee PR 21)

Ported from Melodee's PR 21 "fix(dual-core): raise the core supply and survive CPU1 misreads, faults and stalls" by
Kerem Kilic / Melodee (GPL-3.0, <https://github.com/keremimo/melodee>), adapted to our job hand-off.

**The finding (Melodee's, measured on units):** the FM-1's AC79 runs at ~360 MHz, above JieLi's 320 MHz table, on the
boot loader's core rail: SYSVDD 11 (1.26 V) with the 1.4 V rail (VDC14 3) on its LDO. The stock firmware is
single-core, so no FM-1 was validated with both cores. JieLi's own settings above 320 MHz are DVDD (SYSVDD) 1.35 V and
DCDC14 1.45 V, and its exception auto-fix raises the supplies after a crash. Melodee's crash reports decoded to core 1
misreading the idle mailbox (`0x00200000` where RAM holds 0) and calling a null job, and to core 0's SRAM data going
bad while both cores run. A healthy unit ran both cores clean down to SYSVDD 6 (1.11 V): the margin varies per chip.

**The rails** (`firmware/hal/fm1_power.h`; P33's analog registers through `fm1_sys.h`'s existing `fm1_p33_read` /
`fm1_p33_write`: P3_ANA_CON9 SYSVDD 0.93 V + 30 mV a step, CON6 VDC14 1.25 V + 50 mV a step, CON5 VDDIO read only).
With Dual Core On, `cr_c1_boot` (power-on, IRQs off) records the boot loader's levels, then raises VDC14 to 5
(1.50 V; 4 = 1.45 V until 0.22) and SYSVDD to 14 (1.35 V), a step every 100 us, never lowering, **before** core 1
starts. With Dual Core Off (the default 0.16 .. 0.21; or SAFE MODE, or the single-core package) the rails stay as the boot loader left them: the stock
behaviour, the safe baseline.

**Why VDC14 5 (0.22; fm1-x0x#10 and #21, ChoralRootFM1#9).** The reports after 0.16 settled which rail matters.
zednaked (Jangada), on a unit that misreads, 5-minute runs under load: 11 / 3, 12 / 3 and 13 / 3 fault in about 6 s;
11 / 4 ran 90 s; **14 / 4 ran once and failed once** (misreads from 40 s, a fault at 3.5 min); 11 / 5 clean; 15 / 5
clean for 15 minutes (1.23 M blocks, 0 misreads). fc79, on fm1-x0x 1.0.6 with both cores at 14 / 4: a core 0 crash
every few minutes, decoded to misreads on **core 0** (the TIMER5 vector fetched as `0x0300046E` for `0x0200046E`, bit
24 set; a crash inside the bass render with a flipped high address bit): nothing core 1's check word or its fault
catch can see, since core 0 takes the exception and the crash report reboots the unit. So 14 / 4, what 0.16 chose
as JieLi's own levels for this clock, is exactly the marginal setting, and the raise of the regulator is what the
affected units needed: fm1-x0x 1.0.7 ships VDC14 5 with SYSVDD left at 11 (Charles Vestal, 2026-10-14), Jangada
15 / 5. The AC791N datasheet gives 1.55 V as this rail's limit, and fc79's read of the stock V15 firmware found it
runs the unit at VDC14 7 (1.60 V) and SYSVDD 15 (1.38 V) all the time, so 1.50 V is inside both what the maker's
sheet allows and what the unit ships with. SYSVDD stays at 14: it did not matter in zednaked's table, and the
user's decision after Charles Vestal's review (below) stands for it.

**The validation** (`firmware/hal/fm1_cpu1.h fm1_c1_post / fm1_c1_poll`): core 0 writes fn, arg and a check word
(`FM1_C1_CHECK`: fn ^ arg ^ seq * 0x9E3779B1 ^ a constant), then the sequence number. Core 1 keeps the count of
finished jobs in a register and runs a request only if its sequence is exactly the next one and the check word
matches what it read; fn is never null (the mailbox at rest holds `fm1_c1_idle`, and a null fn is refused anyway).
Anything else is counted (`fm1_c1_rejected`, the last bad sequence in `fm1_c1_misread`) and never run; a real
request read wrong is retried on the next look.

**The escalation** (`cr_cpu1.h cr_c1_assess`, `cr_c1_check` before each hand-over, `cr_c1_wait` after):
- the first misread (an audio block that finds new ones in `fm1_c1_rejected`: one burst is one event) is counted and
  core 1 keeps working;
- a second misread (a later block), or any fault or stall: core 1 held for good, `c1 gave up`, one core with the
  shared budget (the existing path), `cr_dbg.c1_fallback` 1 for the session; **since 0.22 the UI then turns Options >
  Dual Core Off and the settings save it** ("The persistent fallback" below): the next power-on is one core.
**No voltage escalation.** The PR raises SYSVDD to 15 (1.38 V) and VDC14 to 5 (1.50 V) after the first misread; the
user decided against it after Charles Vestal's review of the PR (device safety): the rails go to their levels once,
at power-on, and no rail is ever set above the cap: 14 / 4 (1.35 V / 1.45 V, JieLi's own levels for this clock) in
0.16 .. 0.21, **14 / 5 (1.35 V / 1.50 V) since 0.22** (the paragraph "Why VDC14 5" above). `fm1_power.h` caps them
as hard maxima (`FM1_SYSVDD_MAX`, `FM1_VDC14_MAX`), the test build's rail op included.
The stall timeout stays our 8 ms (the PR uses 10 ms). A faulted or stalled job's block is rendered by core 0 as
before (core 1 is held first, so nothing races it).

**Faults** (`fm1_vec.S` -> `fm1_cpu1.h fm1_c1_fault_c`, before the crash report; after the PR's `fm1_irq.h` /
`fm1_vec.S`): before 0.16 any fault about core 1 reset the chip: a core-1 PC-limit hit (DBG_MSG bit 10) raised the
exception on core 0 and ended in `fm1_fault_c`'s crash report. Now: DBG_MSG with core 1's bits only (6, 8, 10, 11,
16..18): core 1 held, the fault recorded (core, DBG_MSG, EMU_MSG, pc, rets), DBG_MSG cleared, core 0 resumes as an
ISR return; core 1 took it itself (`cnum` 1): recorded, it holds itself; core 0's echo of that is swallowed once.
Core 0's own bits still give the crash report; so does a 17th fault (a hold that did not take). A build without
core 1 (CR_CPU1 0) links `fm1_vec.S`'s weak `fm1_c1_fault_c`: the crash report, as before. Since 0.22 a crash of
core 0's own while core 1 runs first holds core 1 and bars it (below), then the crash report.

### The persistent fallback (0.22; ChoralRootFM1#9, after fm1-x0x 1.0.7)

The reports' crash is core 0's: the misread hits its fetch or its data, the crash report reboots the unit, the
settings still say Dual Core On, core 1 starts again, and the unit crashes again until the boot guard's second count
puts it in SAFE MODE (where core 1 is never started), and after the next power-on the loop starts over. The reporter
of ChoralRootFM1#9 saw exactly that ("a crash/reboot loop"). From 0.22 the unit decides for itself, as fm1-x0x
1.0.7 does:

- **The record** `fm1_c1_bar` (`hal/fm1_cpu1.h`, `.noinit`: a warm reset keeps it, a power-off leaves garbage behind
  a magic): `ran` (core 1 started and answered this boot: set by `fm1_cpu1_start`) and `barred` (why core 1 must not
  start at the next boot: 1 a crash, 2 a hang, 3 given up).
- **Set** by `fm1_c1_fault_c` when the fault is core 0's own and core 1 is on (core 1 held, `barred` = crash, then
  the crash report as before), and by `cr_c1_give_up` (`barred` = given up: a second misread, a fault about core 1,
  a stall), in the audio ISR, before anything reaches the settings. **Core 0's own** means its debug-unit sources
  (`FM1_DBG_C0`: its MMU, PC and write limits, bus-invalid fetch / read / write; fc79's records were DBG_MSG `0x1000`
  and `0x200000`) or no source at all (a plain CPU exception: an illegal instruction read from RAM); a fault that only
  another master raised (`FM1_DBG_PRP`: the watchdog, the peripherals' MMU and write limit, an AXI master's invalid
  read / write, the DMC / DBG pendings) gets the crash report but no bar (`fm1_c1_crash_bars`, host-tested). Found on
  the owner's unit on 2026-10-14: a RESTART with core 1 running once crashed in the soft reset itself (`fm1_reboot`'s
  P33 write, uptime 37.6 s, DBG_MSG `0x10` = `axi_rd_inv`: a DMA still reading as the unit leaves), which the first
  build took for a crash beside core 1 and barred. That restart crash is older than this work (it showed as a 4 s
  crash screen on the way to the reboot, which nobody looked at) and is open: HANDOFF.
- **Decided** at the next boot, `cr_c1_bar_decide` (`cr_cpu1.h`, pure, host-tested), called by `cr_c1_bar_boot`
  (with `bootguard.cls`) from `cr_settings_boot` right after the record's Dual Core bit is read, not in SAFE MODE
  (nothing is saved there, so the record is left for the next normal boot): a wrong magic (a power-on) is nothing; `barred`
  is its reason; `ran` with a **watchdog** reset is a hang (a WDT reset is never intentional, so it counts at any
  uptime, unlike the boot guard's 30 s; a soft reset without `barred` is an intentional reboot or an update: nothing).
  Then the record is cleared for this boot. If the record said On, the decision sets `cr_c1_off` (core 1 is not
  started) and `cr_c1_barred` (the reason, for the UI); if it already said Off there is nothing to do.
- **Saved**: `cr_settings_load` reads Dual Core as Off while `cr_c1_barred` stands (`crs_dc_bar`) and primes the
  automatic save, so the settings' next quiet moment writes the record with bit 6 clear: the power-on after that is
  one core from the record itself. The user's own Options > Dual Core sets the bit again (`cr_settings_dc_user`
  clears the bar). SAFE MODE writes nothing, as ever; the bar in `.noinit` holds through its soft reboot. **With On
  the default (0.22, this line's 3y46)** the bar is what turns a unit Off: `cr_settings_defaults` gives a new unit and
  Reset Settings On, a record's Off stays Off, and a barred power-on reads Off whatever the record or a Reset
  Settings in it says (`crs_dc_bar` until the player sets the option). The safety net of "The start" above is apart:
  it never saves.
- **The single-core package** (`CR_SINGLE_CORE`): inert. Core 1 never starts there, so `ran` is never set and no give-up
  or crash beside it can happen; `cr_c1_bar_boot` clears a record a Standard package left (a warm reset across an
  install) and decides nothing, so it never writes Dual Core (the record keeps the Standard package's choice).
- **P5's chord voices on core 1** (3y46): a give-up runs the same job on core 0 in that block and `voice.c
  p5_pool_enforce` takes the voice counts down to one core's in the next ("The fail-safes"); a barred boot is a one-core
  boot from the start (four P5 voices, a P5 bass one), as Dual Core Off.
- **Shown**: a message after the splash (`dual core off: crash` / `hang` / `gave up`, red), the line under Options >
  Dual Core (`off after a crash on 2 cores`, `off after a hang on 2 cores`, `off: the 2nd core gave up`), GEEK OUT's
  core line (`c1 off · after a crash`), and `fm1_install.py --debug`'s `c1_bar` (78 PEAK's value 69).
- **The test build** (`CR_CORE1_TEST`): an injected misread, stall or fault (ops 1..4) is not the unit's, so it sets
  no bar and the UI does not turn the option Off for it (`core1_fault_test.py` restarts between its modes with Dual
  Core On); op 7 `bar` leaves the bar as a crash would and restarts: the boot path end to end on a healthy unit.
- **The halt at reset** (`fm1_cstart`, first statement: `fm1_cpu1_halt`): a core 1 the reset did not hold would read
  the RAM `fm1_cstart` is clearing (Jangada's finding in fm1-x0x#10; fm1-x0x does the same).

What this does not do: start core 1 again by itself. A unit that was barred stays on one core until its owner turns
Dual Core On again; the option's line says why it is Off. Host tests: `tests/cr_cpu1_test.c` section `bar`. The
emulator (`test_cr.sh`, `EMU_C1=hang`): the give-up's message, one settings save with `dualcore 0` and its erase once
the chord is released (the WAV is one core's up to it), and a relaunch on that flash boots `c1 off`.

**Counters**: `--debug` (`tools/fm1_install.py`, after `c1_jobs`): `c1_rejected`, `c1_faults`, `c1_fallback` (1: core 1 given up this session),
`rail_sysvdd_boot`, `rail_sysvdd_now`, `rail_vdc14_boot`, `rail_vdc14_now`; and, as 78 PEAK's value 69 (73 DEBUG
is full: its count is one 7-bit byte; 35 on the branch that added it, appended after this line's 68 by name),
`c1_bar` (0.22: this boot's reason core 1 was kept off, 0 none; `--debug`
prints the reason in words), then (0.22, PEAK's values 70..78) **the last crash's record** (`hal/fm1_irq.h fm1_crash`,
`.noinit`: what the crash screen shows for 4 s, kept until a power-off: `crash_count`, `crash_vec`, `crash_pc`,
`crash_rets`, `crash_emu`, `crash_dbg`, `crash_uptime_ms`; `--debug` prints it as one line, with the PC to look up in
`build/choralroot.dis`) and this boot's `boot_reset` (P3_RST_SRC bits 0..7, the core reset as bit 8) and `boot_stage`
(the breadcrumb the last reset left, in words): what a reporter could not tell us before (ChoralRootFM1#9 has no
crash record). GEEK OUT: `c1 ok 123 · stk 1064 · rej 0`
(and `· flt n` once there is a fault); given up: `c1 gave up · Options>Dual Core` (the switch; 30 of the
31 characters a line holds, hence no spaces around `>`); barred: `c1 off · after a crash`. Host: rails read 11 / 3 at
boot (14 / 5 with Dual Core On).

**The test build** `CR_CORE1_TEST` (`cr_cpu1.h`, 0 by default; never in a release): SysEx command 75 CORE1_TEST
(`cr_backup.c`): 0 report, 1 a misread, 2 a null job at rest, 3 a stall, 4 a fault on core 1 (a job that calls
address 0), 5 the rails, 6 a rail to a level (down too; SYSVDD 6..14, VDC14 0..5: never above the cap), 7 the bar (0.22:
the record a crash beside core 1 leaves, then 72 RESTART's soft reset; `core1_fault_test.py bar`, not in its default
list: it leaves Dual Core Off). Any other build answers "unsupported"
(the byte 127) and `tools/core1_fault_test.py` refuses to run. `build.py` passes no arbitrary defines, so to build it
set the default in `firmware/src/cr_cpu1.h` to 1 (`#define CR_CORE1_TEST 1`), `./build.sh`, keep the package
(`build/choralroot-core1test.fwsc`), set it back to 0 and rebuild. The user runs it on the device, not the agents.

Host tests: `tests/cr_cpu1_test.c` section 4 (the mailbox validation; the escalation's states and through the audio
blocks, the rails never above 14 / 5; the boot rails On / Off; the rails' steps on a mock P33: one step a write, VDC14 first, never lower, capped at 14 / 5;
the bar's decision and its boot).

### Device checklist (0.16)

0. The release build as installed (Dual Core Off, the default; after an update from 0.15 too): `c1 off`, `c1_state 0`,
   the rails' now equal their boot levels (11, 3); Options > Dual Core reads Off.
1. Options > Dual Core On ("On · restart"), power cycle: `fm1_install.py --debug` shows `rail_sysvdd_boot 11`,
   `rail_sysvdd_now 14`, `rail_vdc14_boot 3`, `rail_vdc14_now 5` (4 until 0.21), `c1_state 1`, `c1_rejected 0`,
   `c1_faults 0`, `c1_bar 0`. GEEK OUT: `c1 ok <jobs> · stk <n> · rej 0`.
2. Dual Core Off, power cycle: the rails' now equal their boot levels (11, 3), `c1 off`.
3. A long chord session (an 8-note chord + bass, 30 minutes or more, USB recording on): `rej` stays 0 (a unit that
   misreads shows `rej` rising; a second time `c1 gave up · Options>Dual Core`, `c1_fallback 1`; tell us), no crash.
4. On a `CR_CORE1_TEST` build: `python3 tools/core1_fault_test.py` (misread, null, stall, fault; each restarts the
   FM-1 and waits 31 s): every mode PASS, the audio running throughout, no reset. `python3 tools/core1_fault_test.py
   power` reads the rails. Then flash the release build back.
5. The persistent fallback (0.22), on the `CR_CORE1_TEST` build with Dual Core On and `c1 ok`: `python3
   tools/core1_fault_test.py bar` (op 7: the bar left as a crash beside core 1 would leave it, then a restart). After
   the restart: the message `dual core off: crash` after the splash, Options > Dual Core reads Off with `off after a
   crash on 2 cores` under it, GEEK OUT `c1 off · after a crash`, `fm1_install.py --debug` `c1_bar 1`, `c1_state 0`;
   power cycle: still Off (`c1_bar 0`: the record saved it), `c1 off`. Options > Dual Core On, power cycle: `c1 ok`
   again. The in-session give-up's save (`dual core off: gave up`) has no test mode: the injected stall and fault
   deliberately do not persist (above); it is the affected units' path, as the crash itself is.
   **Run on the owner's unit 2026-10-14 (HISTORY 3y72)**: steps 0, 1, 4 and 5 as written (the power cycle of step 5
   as a RESTART; the screen not read); the misread / null / stall / fault modes PASS and a restart after the injected
   give-up boots `c1 ok`; the restore of a backup taken with Dual Core On is the way to turn it On again over USB.

## Hardware checklist (when the FM-1 is back)

1. Flash the build, Options > Dual Core On, power cycle: the splash, sound. GEEK OUT (Options > View): `c1 ok <jobs
   rising> · stk <n>` (n well
   under 2816; fm1-x0x saw 828 for its drums). Console: `cpu` -> `c1_state 1`, `c1_start_us` (a few us .. ms).
2. Play perf.sh's (c), (i) and (j) by hand (or their scripts' gestures): the console's `cpu` stages and
   `audio_max_all_us` with Options > Dual Core On and, for comparison, Off (the default); note them here.
3. An 8-note chord + bass notes on the BASS channel: all sound (no voice given up: `voices_given_up` 0; with Dual
   Core Off one is given up).
4. A flash save (SAVE a sound, the settings), a loop slot write: no glitch beyond the known erase silence, no crash.
5. Reboot paths: Options > Flash Data reboot (SAFE MODE), OCT- + OCT+ + HOME 5 s (0.18: the stub's rescue request; until 0.17 OCT- + OCT+ 5 s, UBOOT), the installer's update (M-UPGRADE,
   the loader): each must come back cleanly (core 1 held before each).
6. SAFE MODE (two crash boots): `c1 off`, everything else as before. Options > Dual Core Off (the default), power
   cycle: `c1 off`, `c1_state 0`; back On, power cycle: `c1 ok`.
7. The Transporter dump still works (the boot info / mailbox at the top of RAM are untouched apart from the entry
   word 0x01C7FFF8).
8. Leave it playing (c) for 10 minutes: `c1` stays `ok`, `audio_late` 0.
If step 1 shows `c1 failed`: check `fm1_c1_trace` (0xC1000001: reached its entry; 0xC1000002: reached the RAM loop)
with `fm1t memr`, and that `.c1_text` is in the image (`./build.sh`'s size line).
