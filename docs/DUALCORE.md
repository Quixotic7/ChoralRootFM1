# The second core: the bass part on core 1 (2026-10-09)

**Status: built and host-verified (tests, emulator); not yet tried on hardware** (the FM-1 is bricked; the checklist
at the end is for when it is back).

The FM-1's chip (JieLi AC79 / WL82) has two pi32v2 cores. ChoralRoot starts the second one at power-on and, in every
audio block, hands it the BASS part's render (part 1) while core 0 renders the CHORD part (part 0). With core 1
alive each of the two parts has its own voice budget: 8 voices (16 units: FM6 16) for the chord part and the looper,
8 for the bass part. There is no setting: if core 1 does not answer, ChoralRoot runs on one core as before, with the
shared budget of 8 voices.

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

Start (`cr_cpu1.h cr_c1_boot`, from `main.c fm1_main`, after the sounds and **before `audio_init` and the timers**:
fm1-x0x found that started later it never reaches its entry): the top of RAM unlocked, the PC limits opened
(`fm1_guard_pc_open`: core 1 boots through the chip's ROM, which they refuse; Melodee's finding), `fm1_cpu1_start`
(the stack filled with "C1MK", the entry word, the start sequence, then up to 20 ms for `fm1_c1_alive`), the PC limits
armed again (`fm1_guard_enable(FM1_GUARD_PC)`). No answer: core 1 held, `cr_dbg.c1 = failed`. In SAFE MODE core 1 is
never started (`c1 off`). The boot breadcrumb `BS_CPU1` ("core 1") is set first, so a hang there shows in the boot
guard's `prev_stage` and counts as a failed boot.

Held before every reset and loader entry (`firmware/hal/fm1_sys.h`): `fm1_reboot` (the crash screen, Options > Flash
Data, CR_REBOOT), `fm1_enter_uboot` (OCT- + OCT+ 5 s, USB SysEx, the boot guard's UBOOT), `fm1_core_reset` (and so
`fm1_enter_update`: the update loader). The loader (`firmware/loader`) never starts it.

## Memory (the size line of `./build.sh`)

`size: .text 414120 B, .ram_text 2888 B, .c1_text 60 B (RAMTEXT 2948 of 24576), .data 12072 B, .bss 72104 B;
XIP 429140 B of 581564 (73.8%), RAM 84176 B of 98304 (85.6%), POOL 327000 B of 344064 (95.0%)`

Added: RAMTEXT 60 B (`.c1_text`); RAM ~3.9 KB (core 1's stacks 2816 + 256 B, FM6's second scratch 384 B, the
matrix per part ~150 B, `cr_dbg` ~200 B, the job's state); POOL 256 B (the bass part's block and stereo side,
`c1_buf` / `c1_side`); XIP ~3 KB.

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
draws from the shared generator on a model change) and VOICE on both parts (one breath-noise generator).

What runs where:

| core 0 (the audio ISR) | core 1 |
| --- | --- |
| the engine tick, the looper, MIDI, events, voice allocation for both parts | waits in RAM |
| parts' matrix + LFO tick | |
| part 0's render | part 1's render (`track_render` of trk[1]) |
| waits for core 1 | waits in RAM |
| DIST, level, pan, sends of every part; parts 2..3; FX buses; master; USB tap; click | |

The wait is inside the half's time: the CPU meter and the overload shed (`audio.c shed_check`) see the true block.
Core 1 only works inside the audio ISR, which waits for it before it returns; core 0 turns the flash off (saves,
updates) only with interrupts off; idle, core 1 runs from RAM. So the flash is never off under it.

## The budgets (`voice.c voices_busy_of`, `voice_victim`)

With core 1 alive (`cr_dualcore_active()`): part 1 has its own VBUDGET (16 units) and part 0 with parts 2..3 another;
`voice_room` / `voice_alloc` count and steal only within the part's own budget. The chord part's budget is shared with
the looper's loop voices, which play on part 0. The bass part is MONO in ChoralRoot (voice mode 1 whatever the preset),
so its budget matters for MIDI notes and the loop on the BASS channel. With core 1 off, failed or given up: the shared
16 units of all parts, as before (Felucca, `tests/regress.c`: `CR_CPU1 0`).

CPU: on one core (the emulator's inline job) two full budgets could reach about twice the old worst block, but only
if both parts played 8 voices; with the bass MONO the extra over the old budget is the chord part's 8th voice and the
bass's own. perf.sh (j) (an 8-note VA 13th + the VA bass, 9 voices, one more than one core's budget): 740 / 1218 us of
2902 on one core, of which core 1 takes the bass's 46 / 125.

## The stage profile (`cr_dbg`, GEEK OUT, console `cpu`, perf.sh)

`cr_cpu1.h` marks the ISR's stages per half buffer: the engine tick, part 0, part 1 (core 1's job time when it renders
it), the rest of the mix with the FX, the master with the click, the wait for core 1. Average and maximum of the last
second (`cpu_window`): the console's `cpu` (`stage_<name>_avg_us` / `_max_us`, `c1_state`, `c1_jobs`,
`c1_stack_bytes`, `c1_start_us`) and GEEK OUT's fifth line (`t107 c443 b135 f730 m159 w102`: tick, chord, bass, fx,
master, wait, us per half). GEEK OUT's fourth line: `c1 ok <jobs> · stk <bytes>` (the stack's high-water mark from
the C1MK fill), `c1 off`, `c1 failed`, `c1 gave up`. On the device the stages count TIMER4 ticks; on the emulator
host instructions (EMU_STAGES=1; the counter's own cost measured and left out; device us = instructions / 259).

perf.sh, 2026-10-09 (device estimate from the emulator, us per 2902 us half, avg / max; one core's block):

| scenario | block (one core) | tick | part0 (chord) | part1 (bass) | fx | master |
| --- | --- | --- | --- | --- | --- | --- |
| (c) FM PAD chord + DEEP SUB + loop + arp | 814 / 1048 | 7 / 93 | 507 / 781 | 58 / 169 | 142 / 418 | 58 / 127 |
| (i) (c) on FM TONE STRINGS | 840 / 1075 | 7 / 105 | 534 / 880 | 58 / 177 | 141 / 325 | 58 / 149 |
| (j) 8-note VA 13th + VA bass (MIDI) | 740 / 1218 | 6 / 57 | 461 / 865 | 46 / 125 | 126 / 259 | 58 / 85 |

What this says: ChoralRoot's bass part is MONO, so core 1's job is small (~50-70 us a half on average, up to ~260 in
(g)); the chord part is the big stage and stays on core 0. The win today is the budget (an 8-note chord and the bass
both whole), not CPU: about 5-8 % of a half. A split that also moves part of the chord part (or the FX) to core 1
would be the next step if the CPU is ever short.

On the device with core 1 the half is about the block less part1, plus the wait.

## Options > Dual Core (0.15)

Options > **Dual Core: On / Off** (after USB Level; settings v11, bit 6 of `view`, docs/SETTINGS.md), default On. Why:
other FM-1 firmwares' dual-core support was reported to crash for some people after playing notes; this keeps a
per-device way out if ChoralRoot's ever does. `cr_settings_boot` (persist_boot) reads it into `cr_c1_off` before
`cr_c1_boot`; Off: core 1 is never started, `cr_dbg.c1` stays `C1_OFF` (GEEK OUT `c1 off`, console `c1_state 0`), one
core with the shared 16-unit budget (an 8-note chord + bass gives up a voice). A change in Options takes effect at the
**next power-on** (the value reads "Off · restart" / "On · restart", the line under it "takes effect after a restart"):
core 1 is never started or stopped live. Test: `tools/emu/test_cr.sh` (`cr_dualcore_opt.txt`, a relaunch with
`cr_dualcore.txt`: `c1 off`, one voice given up; `cr_dualcore_on.txt`, a relaunch: `c1 ok`, none given up).

## The fail-safes

- Core 1 does not answer in 20 ms at power-on: held, `c1 failed`, one core for the session, the shared budget.
- A job not done in 8 ms (`fm1_cpu1_wait`): core 1 held for good, core 0 renders part 1 itself in that block (late,
  but the samples are one core's), `c1 gave up`, the shared budget from then on (the next allocations steal back to
  it). A hang in core 1 never hangs core 0.
- A crash on core 1 resets the chip as any crash does (the boot guard counts it as usual).
- Host tests: `tests/cr_cpu1_test.c` (the split bit for bit for 3 x 3 preset pairs, the job before and after part
  0; 8 + 8 voices with core 1, 8 without; a job that never answers; no answer at start). The emulator:
  `EMU_C1=nostart` / `EMU_C1=hang` (`tools/emu/test_cr.sh`, `cr_dualcore.txt`), `EMU_STAGES=1`.

## Hardware checklist (when the FM-1 is back)

1. Flash the build; power on: the splash, sound. GEEK OUT (Options > View): `c1 ok <jobs rising> · stk <n>` (n well
   under 2816; fm1-x0x saw 828 for its drums). Console: `cpu` -> `c1_state 1`, `c1_start_us` (a few us .. ms).
2. Play perf.sh's (c), (i) and (j) by hand (or their scripts' gestures): the console's `cpu` stages and
   `audio_max_all_us` with core 1 (the default) and, for comparison, a build with `CR_CPU1 0`; note them here.
3. An 8-note chord + bass notes on the BASS channel: all sound (no voice given up: `voices_given_up` 0).
4. A flash save (SAVE a sound, the settings), a loop slot write: no glitch beyond the known erase silence, no crash.
5. Reboot paths: Options > Flash Data reboot (SAFE MODE), OCT- + OCT+ 5 s (UBOOT), the installer's update (M-UPGRADE,
   the loader): each must come back cleanly (core 1 held before each).
6. SAFE MODE (two crash boots): `c1 off`, everything else as before. Options > Dual Core Off, power cycle: `c1 off`,
   `c1_state 0`; back On, power cycle: `c1 ok`.
7. The Transporter dump still works (the boot info / mailbox at the top of RAM are untouched apart from the entry
   word 0x01C7FFF8).
8. Leave it playing (c) for 10 minutes: `c1` stays `ok`, `audio_late` 0.
If step 1 shows `c1 failed`: check `fm1_c1_trace` (0xC1000001: reached its entry; 0xC1000002: reached the RAM loop)
with `fm1t memr`, and that `.c1_text` is in the image (`./build.sh`'s size line).
