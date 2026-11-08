# Audio quality: the output chain, what was measured, and Options > Output (0.19)

GitHub issue 3 (a user on 0.17 or older, headphones on the 3.5 mm output): "muffled / harsh compared to other
firmwares, especially in the mid to high frequencies (bass sounds good) ... Sloop sounds a lot clearer, fuller ...
all the engines ... maybe it is just level matching / in the output stage". This document holds what was found
(2026-10-12), what 0.19 changes, and how to measure it again. Inference is marked as inference.

**Two rounds.** The first compared with the Sloop checkout at hand, which was **v2.2**. Sloop was at 2.6 by the time
of the report (2026-10-11) and 2.6 changed its master. The second round, the same day, measured **Sloop 2.6** and read
Felucca up to 1.5.1: [Sloop 2.6 and Felucca 1.5.1](#sloop-26-and-felucca-151-the-second-round), which also says what
of the first round no longer holds. Where this document says "Sloop" without a version it is 2.2. The second round
also gave Clean's limiter its look-ahead: [The look-ahead](#the-look-ahead).

Between the two, **CZ-1 was made 6 dB lower into the mix** and the click the second round saw on it was followed up:
[CZ-1 level](#cz-1-level-019-after-the-second-round).

**A third round**, the same day, after the owner had heard the first two: Clean's limiter holds longer, the chorus
returns in stereo, the click and the USB recording's stems are lined up with Clean, MASTER is read before the audio
starts: [Round 3](#round-3-the-hold-the-chorus-in-stereo-the-click-master-at-power-on). Where the earlier sections
say "the chorus's return is mono", "hold 40 ms" or "the click is 0.73 ms ahead", they describe the tree before it;
each such place carries a note in brackets that names the round that superseded it. The measurements of every round
are kept as they were taken.

**Where things stand at 0.19** (the one summary; the sections below are the record): Options > Output defaults to
Clean, with Classic kept for comparison. Clean: linear to -2.0 dBFS, a limiter that looks 32 samples (0.73 ms)
ahead and holds 200 ms (a hit's 40 ms), MASTER last. The chorus is stereo in both modes. The click and the USB part
pairs are as late as Clean's output. MASTER is read before the audio starts. CZ-1 is 6 dB lower. Not changed: the
level against Sloop (the owner, 2026-10-12: no per-sound loudness matching right now). Nothing in this document was
measured on a unit.

Levels are the Q15 sample before the DAC word's `<< 7` unless said otherwise, in dB re 32768; MASTER at its top
(`master_q12` 4088). "dist" is the harness's measure, explained under [How to measure](#how-to-measure).

## Short answers

- **The signal path is the same Felucca chain in ChoralRoot and Sloop.** At equal part level the same patch gives
  the same output to 0.1 dB and the same spectrum (PHASE BRASS, a dry note: 0.17 -17.9 dB rms, 38.9 dB under the total
  above 2 kHz; Sloop -17.8 and 39.0).
- **What can explain the report on 0.17**: (1) level: at the same MASTER position 0.17's single notes are a median
  9.4 dB (10 LU K-weighted) under Sloop's, its chords 5.6 dB, its power-on sound 13.8 dB; (2) mono: 0.17 sends the
  same word to both ears on 164 of 168 sounds, Sloop's chorus and reverb return in stereo; (3) the mono chorus's comb
  (computed: +2.7 / -3.9 dB, the same in both ears, on every sound with a chorus send, not on the bass). That these
  are heard as "muffled" is inference; that they are there is measured.
- **What cannot**: the FX amounts (Sloop is as wet), the reverb's tone, the DAC set-up (identical, pin for pin), a
  hidden filter. The first round put the limiter and the clip here too (Sloop 2.2 drives its own far deeper and
  distorts more). **Against Sloop 2.6 that no longer holds**: its master now looks ahead, and on a loud chord it
  leaves 8 to 11 dB less of its own above 2 kHz than 0.17's does on the same mix
  ([the second round](#sloop-26-and-felucca-151-the-second-round)). On loud chords at a high MASTER the output chain
  is a candidate for "harsh" after all; it is what Clean replaces.
- **What was wrong all the same, and is what 0.19 changes**: the output chain distorted more than it should (a chord
  with the bass in gain reduction all of the time, about 4 % of distortion; a tanh over the whole range; MASTER in
  front of a 16-bit path, so a low MASTER left a quiet note with a quarter of its bits), and a setting of Felucca's
  that ChoralRoot cannot show could be left running by another firmware ([Left behind](#left-behind)).
- **Not fixed by 0.19's output chain**: the level against Sloop (a decision about the part level and the sounds'
  trims, [Open questions](#open-questions-and-proposals)). The chorus's mono return was not either; the third round
  made it stereo ([Round 3](#round-3-the-hold-the-chorus-in-stereo-the-click-master-at-power-on)).

## Part 1: ChoralRoot main, 0.17 and Sloop, stage by stage

By reading, and by rendering all three on the host (ChoralRoot through its key matrix with `tests/master_measure.c`;
Sloop with a harness that feeds notes to its `voice.c` at its keys' velocity; every render's captured input through a
copy of the master equal to the firmware's output bit for bit). The scripts and the raw tables (1600 lines) are the
measurement's scratch (`build/scratch/part1/RESULTS.txt` of the 0.19 worktree), not in the tree. Paths under
`firmware/src/`.

| # | Stage | ChoralRoot main / 0.17 | Sloop | Effect of the difference |
| --- | --- | --- | --- | --- |
| 1 | per-voice level | `VOICE_FS` 24000 (`dsp.c`) | the same | none |
| 2 | velocity | 100, linear | 100, linear | none |
| 3 | part LEVEL | **92** (-10 dB; `cr_ui.c`: the default minus 12) | **104** (-4 dB) | **-6 dB on every sound** |
| 4 | per-sound trim | seven sounds, all down 3 .. 8 dB (`cr_bank.c`) | every sound, -13 .. +11.5 dB, 26 of 68 at +7 dB or more (`preset_trim.h`) | with 3: single notes a median 9.4 dB under Sloop's |
| 5 | DIST | 0 | 0 | none |
| 6 | mix line | exact (`(x * gl) >> 12`) | `((x >> 4) * gl) >> 8`: each part to multiples of 16 | Sloop is the noisier (same patch: 10 dB more energy above 10 kHz, all of it arithmetic) |
| 7 | pan, dry stereo | centre; VA SPREAD only | centre, mono | none |
| 8 | FX switch, sends | FX on. 0.17: per sound (TINE EP chorus 45, delay 25, reverb 35; default 24 / 28 / 36). main: 35 / 45 / 25 for every sound | no switch; per sound, the same 24 / 28 / 36 default; 61 of 68 sounds have chorus or reverb | wet re dry: 0.17 -7.7 .. -14.2 dB, main -6.2 .. -6.8, Sloop -9.7 .. -13.8: **Sloop is not dry** |
| 9 | chorus | one tap, returned equal to L and R | two taps half a turn apart, L and R | **0.17 and main: a mono comb**, +2.7 / -3.9 dB (computed); Sloop: a different comb per ear |
| 10 | delay | 0.17: mono, full rate. main: stereo | as 0.17 | |
| 11 | reverb | 0.17: ROOM, mono, full rate. main: stereo FDN at 22.05 kHz | stereo FDN, full rate | 0.17's is as bright as Sloop's (share above 2 kHz -0.9 against -1.0 dB) but **mono**; main's lacks the wet above 10 kHz |
| 12 | master sum, MASTER | `((mix + wet) >> 2) * master >> 10`; `(k10 * k10) >> 8` | the same | none |
| 13 | DC blocker | 2 Hz | the same | none |
| 14 | LOWCUT / BASS+ | 0 on a new unit; **no control** | 0; its menu has it | see [Left behind](#left-behind) |
| 15 | limiter | `LIM_T` 18000, the gain eased over 64 samples (`fx_smooth`) | 18000, 4-sample attack, the gain at once | at equal input within 2 dB, ChoralRoot's the cleaner |
| 16 | clip | a tanh over the whole range (`softclip`) | `knee()`: linear to 16384, tanh above | ChoralRoot adds -45 .. -52 dB to a single note where Sloop adds nothing |
| 17 | DAC word | `<< 7`: the word's top 6 dB unused, no dither | the same | none |
| 18 | DAC set-up (`hal/fm1_audio.h`) | | identical in Felucca, Sloop, Melodee, x0x and ChoralRoot, write for write | none |

Measured, the same MASTER position:

| RMS / K-weighted, dB | 0.17 | main | Sloop |
| --- | --- | --- | --- |
| all factory sounds, a single note, median | -26.8 / -24.5 | -25.4 / -23.5 | -17.4 / -14.5 |
| all factory sounds, a 6-note chord, median | -19.7 / -16.8 | -18.7 / -16.0 | -14.1 / -11.0 |
| the power-on sound, a single note, dry | -26.9 / -32.2 | -26.9 / -32.2 | -13.1 / -10.7 |
| the same patch (PHASE BRASS), a single note, dry | -17.9 / -15.6 | (retired) | -8.2 / -5.9 |
| the same patch at nearly the same level (WHEEL GOSPEL) | -21.1 / -19.0 | -21.1 / -19.0 | -19.9 / -17.9 |

| Share of the energy above 2 kHz / 5 kHz, dB re total | 0.17 | main | Sloop |
| --- | --- | --- | --- |
| all sounds, a single note | -22.7 / -38.2 | -24.4 / -37.0 | -25.1 / -45.8 |
| all sounds, a chord, above 2 kHz | -17.4 | -19.3 | -18.3 |
| the power-on sound, a single note | -52.6 / -53.8 | the same | -11.7 / -26.8 |

ChoralRoot's sound set is not darker than Sloop's. Its power-on sound is: TINE EP at D4 is close to a sine, Sloop's
RHODES is not. A first impression, not "all the engines".

The master: one note is already over the limiter's threshold on 33 of Sloop's 68 sounds (2 of 168 on 0.17); a chord
on the shared BRASS with the bass is 19.3 dB over it on Sloop (15.8 dB of gain reduction, dist -22.9 dB) and 7.5 dB on
0.17 (-4.7 dB, dist -28.0 dB). Sloop's output chain is the more distorted of the two as shipped. It is also louder.

### What can explain "muffled / harsh, fine bass, all engines, headphones, 0.17 against Sloop"

1. **Level** (measured). 6 dB from LEVEL 92, the rest from Sloop's trims. Every engine. The reporter's own guess. A
   quieter source is commonly judged duller (inference: the ear's response at lower levels).
2. **Mono** (measured). The width of Sloop's chorus and reverb on headphones; 0.18 made ChoralRoot's delay and
   reverb stereo, the chorus's return is still mono on main.
3. **The mono chorus's comb** (level measured, comb computed, audibility inferred): on every sound with a chorus
   send, not on the bass part (send 0): "mid to high, bass fine".
4. **The power-on sound** (measured): a first impression only.

### What cannot

- **The FX amounts, the tails, the reverb's tone**: Sloop is as wet; FX on against off never lowers the energy above
  2 kHz by more than 0.3 dB on any of the three. Switching the FX off on 0.17 makes it drier and takes the comb away,
  not brighter.
- **The limiter and the clip**: see above. What ChoralRoot's tanh adds to a single note is real but 0.3 .. 0.6 %,
  low order (inference: not "harsh").
- **The DAC**: below.
- **The identical stages**: 1, 2, 5, 12, 13, 17, 18.

### The DAC, and "a built-in filter"

The four lines `fm1_audio_init` sets (PC0, PC1, PC2, PC6; the HAL's comment calls them "codec control lines") are
the I2S bus itself: JieLi's AC7911B data sheet names PC0 ALNK0 MCLK, PC1 SCLK, PC2 LRCK, PC6 DAT3, and the HAL uses
channel 3. None of them is a strap (a de-emphasis, a filter select, a mute); the writes only make them outputs before
ALNK0 takes them. The DAC chip is not identified anywhere in the material at hand (no schematic, no part number in any
firmware's source; the stock firmware's package is opaque); no firmware drives a codec register, an amplifier enable
or a volume. Whatever filter the DAC has is fixed on the board and the same for every firmware: it cannot be the
difference to Sloop. The HAL is untouched.

**The power-on pop** (HISTORY 3y267) is not in the samples: ChoralRoot's output is digital zero from power-on, and
FM1-Base starts ALNK0 the same way; no firmware drives an amplifier enable. Not found yet.

Two facts from this: the samples are right-aligned in the 32-bit word (not "left-justified" as the HAL's comment
says), and **the word has 6 dB above what ChoralRoot, Felucca and Sloop send** (`OUT_SHIFT` 7: the Q15 sample's full
scale is half the 24-bit word's). fm1-x0x has an option that uses it ("LOUD: 6 dB more at the headphones and line
out", `fm1-x0x/firmware/src/app/engine.c`). See [Proposed](#proposed-not-built-in).

## What runs between an engine and the DAC on a freshly reset unit

The owner's question: could something be switched on silently? Everything, with its default, on 0.17 and main. The
effect is on a single dry note.

| Stage | Default | Seen / switchable | Effect |
| --- | --- | --- | --- |
| the sound's own patch (filter, envelopes, its LFOs; FM6's 20 Hz DC filter) | the sound's | yes (EDIT) | the sound |
| the part's LFO, Felucca's matrix (`mod.c`), motion | the LFO runs, every depth 0, no slot | no | none |
| velocity | 100 | yes (Options) | Felucca engines: -2.1 dB against 127 |
| voice mode, glide, detune | POLY (bass MONO), 0, unison only | yes | none |
| part LEVEL | 92, both parts | the sound's pages; the bass: a knob | -6 dB against Sloop's 104; no spectral effect |
| per-sound trim | 0; seven sounds down | no | -3 .. -8 dB on those |
| FX switch | **on** | yes (FX) | 0.17: off closes the chord part's sends only, the bass keeps reverb 10; main: both |
| chorus at 45 | on (0.17: TINE EP's; main: every sound) | yes | wet -8.9 dB; +0.3 .. +0.8 dB; the mono comb |
| reverb at 35 | on | yes | wet -14 .. -19 dB (0.17), -10 .. -14 dB (main) |
| delay at 25 | on | yes | wet -17 .. -18 dB (0.17), -10 .. -16 dB (main) |
| Felucca's FX hold layer, SLICER | not built on main; inert on 0.17 | | none |
| **LOWCUT / BASS+ (`settings.lowcut`)** | 0 on a new unit | **no: neither shown nor switchable** | [Left behind](#left-behind) |
| DC blocker, limiter, tanh clip | always | no (0.19: Options > Output) | Part 2 |
| USB Level, Dual Core, USB Record, the click | Master, Off, On, silent | yes | none (Dual Core: the same mix code) |
| a speaker EQ, a headphone detection | none exists | | |

None of these defaults, switched off, raises the share of the energy above 2 kHz on 0.17. The measurable gains there
are level (LEVEL, the trims) and stereo returns.

## Left behind

**Felucca's SPEAKER setting survived a flash to ChoralRoot, and ran with no way to see it or switch it off.**

Felucca's settings (`panel.c` `settings {magic, palette, lowcut, zoom}`) are the head of the flash object ChoralRoot
keeps its own block in (`settings_persist.c` `persist_t`, object 0 at 0xFC000 / 0xFD000; no installer erases the data
sectors). `lowcut` is Felucca's MENU > SPEAKER: 1 LOWCUT (two high-passes near 110 Hz), 2 BASS+ (near 220 Hz, plus
harmonics made from the bass, `fx.c spk_bass`), in the master, on everything, the USB recording included. ChoralRoot
read it (`settings_import`), applied it (`settings_init`: `fx_lowcut = settings.lowcut % 3`), wrote it back with every
save (`settings_export`), carried it in a backup and a restore (`cr_backup.c`), and did not clear it with Options >
Reset Settings. Its own UI never had the entry.

- **Who can have it**: a unit that ran **Felucca 1.0 or later** (a PER4 object, the same layout) with SPEAKER at
  LOWCUT or BASS+ and was then flashed with ChoralRoot. Current Sloop and current Melodee write the object at another
  size, which ChoralRoot rejects (defaults); their older builds wrote sizes it accepts. fm1-x0x uses another storage
  magic.
- **What it did** (0.18's emulator on a seeded flash against a clean one, a CZ-1 chord with the bass, the same
  script; `tools/emu/seed_lowcut.py`):

  | band | LOWCUT | BASS+ |
  | --- | --- | --- |
  | under 100 Hz | -17.5 dB | -18.8 dB |
  | 100 .. 300 Hz | -1.5 dB | -4.0 dB |
  | 300 Hz .. 1 kHz | -0.3 dB | -1.4 dB |
  | above 1 kHz | 0 | -0.2 dB |
  | total | -1.3 dB | -2.4 dB |

- **Does it explain the report?** No, as it stands: it thins the bass, and the report says the bass is good. It is a
  defect of its own, found while looking.
- **0.19**: ChoralRoot holds the stage off. `cr_settings_boot` sets `settings.lowcut` and `fx_lowcut` to 0 before
  anything applies them (the flash object, a rejected one, and the copy in RAM that survives a reset all go through
  it); the next write of the object stores 0, so the flash is clean for whatever firmware comes next; a restore
  (`cr_backup.c crb_commit`) stores 0; Reset Settings clears it. Felucca's own builds and tests are unchanged
  (`panel.c`, `settings_persist.c` untouched). In both Output modes. `tools/emu/test_persist.sh` boots on flash images
  seeded with BASS+ in a ChoralRoot object and LOWCUT in a Felucca one.
- **On 0.17 / 0.18 today**: Reset Settings does not clear it and neither does switching off. What does: restoring a
  backup whose settings object has it off (a backup made on the affected unit carries it), or flashing Felucca,
  setting MENU > SPEAKER to OFF there, and flashing ChoralRoot again. Updating to 0.19 clears it.

Everything else ChoralRoot can inherit from another firmware's flash, read from the code:

| Value | Object | Written compatibly by | ChoralRoot | A foreign value's effect on the sound | Can be left behind |
| --- | --- | --- | --- | --- | --- |
| `lowcut` | settings (0) | Felucca 1.0+, older Sloop / Melodee | used, not shown, not checked (until 0.19) | the table above | **yes: fixed in 0.19** |
| Felucca's palette, `zoom` (LEDs) | settings | Felucca, older Melodee | read, overridden or unused | none | yes, harmless |
| the panel calibration | settings | Felucca, older Melodee / Sloop | used, shown (Calibrate), validated | none (a wrong table swaps controls) | yes, intended |
| `bold` (Hold Time) | settings | Felucca 1.0 | used, shown, validated; Reset clears | none | yes, visible |
| favourites | settings | Felucca, Melodee | not used, passed through | none | yes, inert |
| ChoralRoot's block, the session | settings, 18 | ChoralRoot only | own magic and checksum | (cannot be made by another firmware) | no |
| user sounds (32 slots) | 5, 6 | Felucca, Sloop, older Melodee | appear as user presets after the factory ones; validated, clamped | only when picked | yes, visible |
| FM6 bank voices | 7 | Felucca 1.0, Melodee | bank voices; validated | only for a sound that points at one | yes, not silent |
| Felucca / Sloop projects (track levels, sends, filters, patterns, tempo, tune) | 1 .. 4 | Felucca, Sloop, Melodee | **never read as projects**: `project.c` is not in ChoralRoot's unit; the same sectors are ChoralRoot's sound stores, whose own magic rejects them | none: the parts' parameters come from the tables' defaults | bytes stay, never interpreted |
| samples, slices, motion, song chain, MIDI and tune globals | in projects and sample slots | Felucca | not built | none | no |

`lowcut` is the only value another firmware can leave that changes ChoralRoot's sound without being seen.

## Sloop 2.6 and Felucca 1.5.1 (the second round)

Sloop's sources read from its checkout (now v2.6) and from the first round's copy of v2.2, file against file; both
built on the host with the first round's harness (the 2.6 master's shadow: its `lim_block` and `knee`, checked
against `mix_block`'s own output on all 52 renders: exact) and the same renders. Felucca v1.0.1 .. v1.5.1 from
GitHub's compare and the 19 commits' diffs (the local checkout could not be read with git from this worktree). The
scripts and raw tables are scratch (`build/scratch/part1/` of the round's worktree).

### What Sloop changed between 2.2 and 2.6 on the way to the DAC word

| Stage | 2.2 | 2.6 | Effect |
| --- | --- | --- | --- |
| limiter (`fx.c lim_block`, 2.6) | per sample: the envelope 4 samples behind the peak, the gain at once, `(x >> 4) * g >> 11` | **looks one block ahead**: the output is the mix 32 samples (0.73 ms) late; per block the peak of the 32 samples in the delay and the 32 new ones (a scan of both: 128 compares a block), the envelope at that peak at once; the gain a straight line across the block to `LIM_T / envelope` (one divide a block); a 64-bit product. Threshold still 18000. A **hold** of 48 blocks (35 ms) restarted by any peak within 0.5 dB of the envelope, then a release of 46 ms (`LIM_REL` 11; it was 93 ms). The delay is two int32 lines of 32 in RAM (`.bss`); a second copy of all of it for USB AUDIO = FULL | no sample leaves the limiter over 18000 (-5.2 dBFS: every loud render peaks exactly there; 2.2's went to -3.3). Their notes: "loud kicks and basses no longer crackle with MASTER turned up", "a whistle on loud basses" (the 12-bit product, and the gain creeping back at every cycle before the hold) |
| clip (`knee`) | linear to 16384, tanh above | the same | none; but nothing over 18000 reaches it now, so it bends only the top 0.8 dB of a limited peak |
| MASTER | a step a UI frame | ramped a sample across the block (`master_cur`) | no zipper on the knob |
| part level, trims (`preset_trim.h`, `tools/level_presets.py`) | LEVEL 104; a trim for each of 68 sounds | LEVEL 104; **153 sounds**, the 68 old trims unchanged, the 85 new ones made the same way: BS.1770 integrated loudness of a phrase that fits the sound to **-15 LUFS** at the default LEVEL (held chords -16), in half dB, clamped to LEVEL 127: 35 of 153 sit at the clamp (+11.5 dB) and are still under the target | the same 68 sounds: a single note the same to 0.1 dB (median; -0.7 at most), a chord 0.9 dB **quieter** (the limiter no longer overshoots). The 85 new sounds are a median 3.4 dB quieter than the old 68 on a single note |
| mix line | `((x >> 4) * gl) >> 8` | the same | none |
| new stages, all off or at 0 until asked | | per-track INSERT and FILTER, TEXTURE, the metronome, punch-in FREEZE, send throws | none on a plain note (bit for bit when off, by their comments) |
| kick voice (`drum_synth.c`) | level steps at a block's edge | the level glides exactly to its target (`a0 + r (i + 1) / CTL`) | "a whistle on the kicks": tones at 1.4 / 2.8 kHz gone. (ChoralRoot has no drum voice.) |
| chorus, reverb, delay | two taps L / R; stereo FDN; mono delay | the same (a FREEZE path added) | none |
| `OUT_SHIFT`, dither, the DAC set-up | 7, none | the same | none |
| USB audio | 44.1 kHz | 48 kHz too (a polyphase FIR), a full-level tap with its own limiter | not this document's |

### The same renders, Sloop 2.6

Every factory sound (2.6: 153; FX as loaded, no bass, velocity 100, MASTER full), medians; ChoralRoot's from the
same sweep of this tree (Classic reproduces the first round's main to 0.1 dB):

| RMS / K-weighted, dB | 0.17 | main Classic | main Clean | Sloop 2.2 (68) | Sloop 2.6, the same 68 | Sloop 2.6, all 153 |
| --- | --- | --- | --- | --- | --- | --- |
| a single note, median | -26.8 / -24.5 | -25.4 / -23.5 | -27.9 / -26.0 | -17.4 / -14.5 | -17.4 / -14.4 | -19.1 / -16.8 |
| a 6-note chord, median | -19.7 / -16.8 | -18.7 / -16.0 | -20.6 / -18.4 | -14.1 / -11.0 | -15.1 / -12.0 | -15.7 / -12.2 |
| the power-on sound, a single note, dry | -26.9 / -32.2 | -26.9 / -32.2 | -29.3 / -34.7 | -13.1 / -10.7 | -13.1 / -10.7 | |
| the loudest chords (six notes and the bass, FX) | | -14.0 .. -14.8 / -11.8 .. -12.6 | -12.3 .. -13.5 / -10.5 .. -11.2 | -13.0 .. -13.6 / -10.6 .. -11.8 | -13.9 .. -15.1 / -12.0 .. -12.7 | |

| Share of the energy above 2 / 5 / 10 kHz, dB re total (medians) | main Classic | main Clean | Sloop 2.2 (68) | Sloop 2.6 (153) |
| --- | --- | --- | --- | --- |
| a single note | -24.3 / -37.0 / -48.8 | -24.7 / -37.5 / -49.0 | -25.1 / -45.8 / -54.2 | -24.1 / -40.0 / -49.3 |
| a 6-note chord | -19.3 / -34.9 / -46.6 | -19.4 / -35.0 / -47.5 | -18.3 / -36.2 / -51.6 | -15.7 / -33.3 / -46.1 |

**The level gap to Sloop 2.6**, the same MASTER position (all 153 of its sounds; in brackets against the 68 it shares
with 2.2):

| | 0.17 | main Classic | main Clean |
| --- | --- | --- | --- |
| a single note (median) | 7.7 dB, 7.7 LU (9.4 dB) | 6.3 dB, 6.7 LU (8.0 dB) | 8.8 dB, 9.2 LU (10.5 dB) |
| a 6-note chord (median) | 4.0 dB, 4.6 LU (4.6 dB) | 3.0 dB, 3.8 LU (3.6 dB) | 4.9 dB, 6.2 LU (5.5 dB) |
| the power-on sound, a single note | 13.8 dB | 13.8 dB | 16.2 dB |
| the loudest chords (loudness) | | about level | **1 to 2 LU over Sloop 2.6** |

The first round's 9.4 / 5.6 / 13.8 dB (0.17 against 2.2) become 7.7 / 4.0 / 13.8 dB against 2.6: the single-note gap
is smaller only because 2.6's 85 new sounds are quieter ones; on the sounds both versions have it is the same 9.4 dB,
and the chords' gap is 1 dB smaller because Sloop's limiter no longer overshoots. Sloop is still as bright as
ChoralRoot's set and no brighter on a single note; its chords have 3.6 dB more above 2 kHz than Classic's (median).

**The master's own work.** One note is over Sloop 2.6's threshold on 76 of 153 sounds (0.17: 2 of 168; Clean: 0 of
135), a chord on 142. Its own loud chords (CZ BRASS, SYN BRASS, RHODES, each with the bass) run 9.7 to 18.7 dB of gain
reduction all of the time. What the chain leaves ("dist", and the same above 2 kHz relative to the output's own
content there, "top"):

| dist / top, dB | Sloop 2.2 | Sloop 2.6 | 0.17 / Classic | Clean (with the look-ahead) |
| --- | --- | --- | --- | --- |
| Sloop's CZ BRASS + bass, FX, as it plays (19 dB over) | -23.3 / -17.1 | -26.5 / -24.6 | | |
| Sloop's RHODES + 808 (the power-on pair, 17 dB over) | -25.9 / -25.6 | -29.3 / -29.2 | | |
| **the same input for all four**: ChoralRoot's VA chord6 + bass, FX | -30.2 / -27.2 | -33.2 / -32.8 | -29.4 / -25.0 | -52.6 / -53.5 |
| the same: WHEEL | -29.3 / -27.9 | -31.4 / -30.9 | -28.4 / -26.2 | -40.2 / -39.1 |
| the same: CZ-1 | -27.1 / -14.7 | -31.2 / -28.5 | -28.2 / -17.1 | -42.1 / -40.8 |
| the same: CZ-1, velocity 127 | -27.0 / -16.8 | -30.7 / -28.5 | -28.1 / -18.6 | -40.0 / -39.4 |
| all sounds, a 6-note chord, median dist | -29.5 | -35.1 | -33.4 / -32.5 | -69.4 |
| the transient test, the kick 6 / 12 dB over ([below](#the-look-ahead)) | -19.7 / -13.0 | -19.8 / -13.4 | -20.6 / -13.8 | -22.9 / -14.0 |

(The same input: ChoralRoot's captured mix put through each master at its own input line; Sloop's and Classic's
thresholds are the same 18000, Clean has 5.7 dB more room. "dist" counts a limiter's fast gain movement as well as
what it bends: on the kicks it is nearly all gain movement, in every chain.)

On the same loud chord Sloop 2.6's master is 2 to 4 dB cleaner overall than 2.2's and than Classic's, and above 2 kHz
5 to 11 dB cleaner than Classic's (CZ-1: -28.5 against -17.1). Clean is 9 to 20 dB under Sloop 2.6 on the same input.

### What the first round got wrong

- "The limiter and the clip cannot explain the report: Sloop drives its own far deeper and distorts more." True of
  2.2. **Against 2.6 it is the other way round above 2 kHz**: at the same input Sloop 2.6 leaves 5 to 11 dB less
  there than 0.17's master, and as each firmware ships its loud chords Sloop 2.6's -25 .. -29 dB in the top stands
  against 0.17's -17 .. -26 dB.
- "At equal input within 2 dB, ChoralRoot's the cleaner" (the stage table's row 15): within 1 dB of 2.2 either way;
  2 to 4 dB behind 2.6.
- "Sloop: limiter 18000, 4-sample attack, the gain at once", "one int16 ... the same Felucca chain": the chain of
  2.2. 2.6's limiter is described above.
- The level figures were against 68 sounds; the table above has both.

Everything else of Part 1 was read again in 2.6 and stands: the part LEVEL (104 against 92), the trims, the mix line,
the chorus's two taps, the stereo reverb, the knee at 16384, `OUT_SHIFT`, the DAC set-up.

### Felucca v1.0.1 to v1.5.1: every change on the way to the DAC word

19 commits. **None touches the limiter, the soft clip, the threshold, `OUT_SHIFT`, the DC blocker, the mix line, a
voice's level or velocity, or adds dither, a loudness option or a look-ahead**: `master_out` is v1.0.1's line for
line, `dsp.c` and `hal/fm1_audio.h` are identical. Upstream still has the gain that re-attacks on every peak, which
ChoralRoot smoothed (`fx_smooth`) before 0.17.

| Commit (version) | What | Why (theirs) | ChoralRoot |
| --- | --- | --- | --- |
| db70550 (1.0.2) | USB LEVEL FIXED: MASTER after the master for the DAC, the full level to USB (`fx_usb_fixed`, `usb_fixed_dac`) | #42: record with the speaker turned down | **has it** (`fx.c`; its comment says "Felucca 1.0.5": it is 1.0.2's) |
| db70550 (1.0.2) | BASS+: the low-pass 4 poles instead of 2 (`spk_bass`) | #42: it cancelled up to 6 dB of 300 .. 600 Hz | **has it** (held off since 0.19) |
| 7414269 (1.0.5.2) | `rev_clear` cleared one int16 too few of ROOM's all-pass | PR #121: a faint click after a REVERB TYPE change | does not have it; **not on the unit** (Felucca's reverb is not built: `CR_FX` 1). Two lines if a `CR_FX=0` build is ever shipped |
| 276f72a (1.1.5) | MASTER read before the audio starts (`master.c master_boot`), polled under the splash | #137: MASTER was 2048 (70 % of the knob) until the first read after the splash, then a glide: keys sounded loud with the knob down | did not have it; **ported in the third round** ([MASTER at power-on](#master-at-power-on): `master.c`; nothing sounds under ChoralRoot's splash, the window was the half second after it) |
| 276f72a (1.1.5) | the metronome (`click.c`): after the master, after the USB tap, a hard clamp | "never through the limiter, never into a recording" | has its own (`cr_out.c cr_click_mix`), at the same place |
| 276f72a (1.1.5) | USB audio at 48 kHz (`uac_fir.h`) | | the USB agent's |
| 88f1030 (1.4) | BASS+ harmonics x 1.5 instead of x 3 | #180: the limiter pulled the mix down up to 16 dB on each kick | does not have it; dormant (BASS+ is held off). **Take the one line if BASS+ ever gets a control** |
| 88f1030 (1.4) | HALL: a stereo 8-line FDN at 22.05 kHz, the default reverb | ROOM's combs ring | has something else (`cr_fx.c`) |
| 88f1030 (1.4) | SPREAD: POLY voices left and right of PAN | #148 | the VA has its own stereo; the other engines none. A feature |
| 88f1030 (1.4), 129a4cf (1.5), a4d7ca2 (1.5.1) | FX-layer flanger / phaser, LFO 2 SYNC, ENV SYNC, per-track INSERT, dotted delay times, send throws, ANALOG's SYNC / SUB / filter types | features | not built or not applicable (FX layer, sequencer and ANALOG are not ChoralRoot's); INSERT would cost POOL |
| db70550, 2da8a71, 276f72a, f21d6d6 | drum kits, SAMPLE / GRAIN / SLICE fixes ("no click" on an empty slot), FM6 bank storage | | not built (no drum, sample or slice engine); storage only |

For the owner: **the power-on MASTER (#137)** is the one fix on the live path ChoralRoot lacked (taken in the third
round); nothing upstream helps the limiter, the clip or the level.

### Verdict

- **Distortion.** Classic (and 0.17, the same master): on loud chords at a high MASTER it now stands **behind**
  Sloop 2.6, by 2 to 4 dB overall and 5 to 11 dB above 2 kHz at the same input. Clean: **ahead** of Sloop 2.6 by 9 to
  20 dB on the same input, and by more as each ships (it has 5.7 dB more room and its sounds are not driven into
  it): the median chord leaves -69 dB where Sloop 2.6's leaves -35. On a single note Classic's tanh adds 0.3 .. 0.6 %
  where Sloop's and Clean's chains add nothing.
- **Level.** At the same MASTER ChoralRoot is under Sloop 2.6 by a median 6.3 dB (Classic) or 8.8 dB (Clean) on a
  single note, 3.0 / 4.9 dB on a chord, 13.8 / 16.2 dB on the power-on sound; on its loudest chords it is level with
  Sloop 2.6 (Classic) or 1 to 2 dB over it (Clean). The gap is a matter of the quiet end: the part LEVEL (6 dB) and
  the trims, which 0.19's output chain does not touch.
- **The issue, the most likely reading now** (inference from the measurements; nobody has compared the two on a
  unit): the reporter was on 0.17 or older against Sloop 2.5 or 2.6. (1) Level, as before: single notes and the
  power-on sound 8 to 14 dB quieter, heard as dull. (2) New with 2.6: on full chords with MASTER up, 0.17's limiter
  and tanh put their residue 17 to 26 dB under the 2 .. 8 kHz content where Sloop 2.6 puts 24 to 33 dB: "harsh in
  the mids and highs, bass fine" fits that, on every engine. (3) Mono, and the mono chorus's comb, as before.
  0.19's Clean answers (2); (1) and the chorus are the open questions below. (The chorus: superseded by the
  third round, which made it stereo, [The chorus, stereo](#the-chorus-stereo). (1) stays open.)

## Part 2: the output chain of 0.19, and Options > Output

**Options > Output: Classic / Clean** (the entry after Dual Core; an Option: saved by itself; default Clean: `cr_ui.c`,
the settings record's cleared bit). Classic is 0.18's master, bit for bit. Clean is a second chain on the same mix. Switching while
sound plays is click-free: the mix is faded to nothing over 11.6 ms, the chain changes at that zero with its limiter
at rest, and it fades in again (`fx.c out_fade`).

### Classic (0.18)

`((mix + wet) >> 2) * MASTER >> 10` -> DC blocker -> limiter (`LIM_T` 18000, 93 ms release, the gain as
`(x >> 4) * g >> 11`) -> `softclip` (tanh over the whole range) -> `<< 7`.

What was measured on it: a 6-note chord with the bass at full MASTER is in gain reduction 100 % of the time on VA,
WHEEL and CZ-1 (2.6 .. 3.8 dB on average, to 8 dB) and comes out with dist -28 .. -29 dB (4 %); in the 2 .. 8 kHz band
that is only 17 .. 26 dB under the band's own content. A single note, the limiter idle: -43 .. -50 dB, the tanh alone.
MASTER is a gain in front of a 16-bit path: at -24 dB a quiet note's error is -32 dB of the note (TINE EP) to -48 dB,
at -40 dB it is -16 .. -32 dB.

### Clean (0.19)

```
mix + wet  --x 0.75 (CLN_TRIM), kept in Q17-->  DC blocker  -->  a delay of 32 samples (0.73 ms)
           -->  limiter (CLN_T 26000, its gain fitted to the peaks still in the delay: hold 200 ms, 40 ms after a
                hit (round 3; it was 40 ms), release 186 ms)
           -->  knee (linear to 26000, tanh above: a safety)  -->  x MASTER  -->  the DAC's 24-bit word
```

| Decision | What | Why |
| --- | --- | --- |
| the clip | linear up to `CLN_K` 26000 (-2.0 dBFS, the limiter's threshold), then `K + R tanh((a - K) / R)`, `R` = 32767 - `K`, to full scale. (Before the look-ahead: linear to 24576, and a limited peak ran 0.5 dB into the curve) | Sloop's `knee()` / Felucca's `soft_knee`, placed at the threshold: with the look-ahead no sample leaves the limiter over it (a test), so the curve is a safety net that nothing in normal use meets; everything comes out as it went in but for the limiter's gain |
| where the headroom comes from | 3.2 dB from the limiter's threshold (26000 instead of 18000: Classic limits at 18000 because its tanh is already 0.8 dB down there; a knee that is straight up to the threshold does not need that), 2.5 dB from a fixed trim in front of it (`CLN_TRIM` 3072 / 4096) | 5.7 dB in all: the chord with the bass that was 5 .. 8 dB over Classic's threshold is 0 .. 2.5 dB over Clean's, and under it most of the time |
| where the loudness goes | chords with the bass come out 0.4 .. 1.4 LU **louder** than Classic (the limiter takes less); a single note and FM6's chords 1.2 .. 2.5 LU **quieter** (the trim). Nothing is given back after the limiter in 0.19's default build: there is no room left under the Q15 sample's full scale. The DAC word has 6 dB above that: [Proposed](#proposed-not-built-in) | the owner's condition (the speaker not noticeably quieter) is met for chords and not for single notes: his decision, with the listening files |
| the limiter | a safety. The peak is held 200 ms, held again by every peak within 1.2 dB of it, a hit's 40 ms ([Round 3](#the-hold); until then: 40 ms (a period of 25 Hz), again within 0.3 dB), then released over 186 ms; the gain glides in a straight line over the 32 samples before the peak comes out of the delay ([The look-ahead](#the-look-ahead); before it: eased down over 1.5 ms after the peak); applied as an exact product (`clean_gain`) | Classic's gain moved with every period of a bass note (0.46 dB at 40 Hz in the test; Clean's: 0.000 dB). `(x >> 4) * g >> 11` dropped four bits and shifted the sample by up to 16 while limiting |
| MASTER | last, after the knee, into the 24-bit word | the knob turns the level down and nothing else: the limiter's work no longer depends on it, and a low setting keeps the resolution |
| bits | the chain's samples are Q17 (the two bits Classic's `>> 2` shifts out are kept: the default trim is `x 3`, nothing lost); the DAC word gets the 7 bits under the Q15 sample (`out_frac`, added in `audio.c`) | the buffers are int32 and the DAC takes 24 bits: no dither is needed, the bits are simply not thrown away |
| LOWCUT / BASS+ | not in the Clean chain | ChoralRoot holds them off (above); if they get a control, they go in |
| USB audio | the master pair is the Q15 sample as before, taken after the limiter: the same delayed, limited signal the DAC gets. USB Level = Master: after MASTER (as before). Fixed: the full level, taken after the knee and before MASTER (`clean_split`, then `clean_post` in `usb_fixed_dac`); the DAC word is the same either way | against Classic the recording's master pair is 2.5 dB lower on a single note and up to 1.4 dB higher on a loud chord, and peaks at the limiter's ceiling, -2.0 dBFS (Classic's tanh kept it under -3.5 dB; before the look-ahead an overshoot could reach full scale); the two part pairs (pre-master) are unchanged (superseded by the third round: in Clean they are delayed 32 frames with the master pair, [The click and the recording's stems](#the-click-and-the-recordings-stems)) |

### Before and after

(The first round's tables: Clean **before the look-ahead**. With it: [The look-ahead](#the-look-ahead), "Before and
after the look-ahead".)

Full MASTER, the held 4 s of each render (`tools/master_measure_run.sh`; four sounds: FM6 TINE EP, VA SYNTH BRASS,
WHEEL FULL ORGAN, CZ-1 BRASS 1; "chord6" = six notes, "+ bass" = with DEEP SUB; FX at the power-on sends).

| Render | Classic: GR share, dist, loudness | Clean: GR share, dist, loudness | Clean - Classic |
| --- | --- | --- | --- |
| FM6 single, dry | 0 %, -48.4 dB, -24.7 LKFS | 0 %, -76.2 dB, -27.1 | -2.5 LU |
| FM6 chord6 + bass, FX | 20.8 %, -39.1 dB, -13.7 | 1.1 %, -94.7 dB, -15.0 | -1.3 LU |
| FM6 chord6 + bass, FX, velocity 127 | 24.7 %, -38.5 dB, -13.3 | 3.6 %, -79.4 dB, -14.3 | -1.0 LU |
| VA single, dry | 0 %, -49.6 dB, -18.6 | 0 %, -89.3 dB, -21.0 | -2.4 LU |
| VA chord6, no bass, dry | 84.9 %, -30.7 dB, -11.8 | 0 %, -97.0 dB, -13.0 | -1.2 LU |
| VA chord6 + bass, dry | 99.9 %, -29.5 dB, -11.9 | 0 %, -99.5 dB, -11.5 | +0.4 LU |
| VA chord6 + bass, FX | 99.9 %, -29.4 dB, -12.2 | 9.6 %, -53.6 dB, -10.9 | +1.2 LU |
| WHEEL single, dry | 0 %, -43.4 dB, -18.2 | 0 %, -89.0 dB, -20.7 | -2.4 LU |
| WHEEL chord6 + bass, dry | 100 %, -29.0 dB, -11.7 | 12.8 %, -46.7 dB, -10.9 | +0.8 LU |
| WHEEL chord6 + bass, FX | 100 %, -28.4 dB, -11.8 | 29.5 %, -41.1 dB, -10.5 | +1.3 LU |
| CZ-1 single, dry | 0 %, -44.2 dB, -19.0 | 0 %, -89.8 dB, -21.4 | -2.4 LU |
| CZ-1 chord6 + bass, dry | 100 %, -29.3 dB, -12.2 | 2.1 %, -99.3 dB, -11.8 | +0.4 LU |
| CZ-1 chord6 + bass, FX | 100 %, -28.2 dB, -12.6 | 28.8 %, -43.4 dB, -11.2 | +1.4 LU |
| CZ-1 chord6 + bass, FX, velocity 127 | 100 %, -28.1 dB, -12.6 | 45.8 %, -41.0 dB, -10.9 | +1.7 LU |

The distortion by band (dB under the band's own content; the loudest case of each sound, chord6 + bass with FX):

| | Classic: under 300 Hz / to 2 kHz / to 8 kHz / above | Clean |
| --- | --- | --- |
| FM6 | -40.5 / -29.9 / -10.7 / -7.6 | -101.2 / -89.7 / -34.1 / -29.7 |
| VA | -31.0 / -28.9 / -25.0 / -22.3 | -55.1 / -52.5 / -54.5 / -53.6 |
| WHEEL | -29.8 / -28.1 / -26.2 / -11.1 | -41.9 / -40.6 / -40.5 / -28.8 |
| CZ-1 | -29.3 / -27.5 / -17.1 / -12.0 | -43.4 / -43.4 / -38.6 / -39.1 |

(FM6's TINE EP has almost nothing above 2 kHz: in Classic what is up there is mostly the chain's own error.) What
Clean has left on the loudest chords is the limiter catching a peak (its 1.5 ms attack); the knee's share is under
-62 dB everywhere. (The second round: it is the gain's steps, not the attack's speed, and the look-ahead does not
lower it: [The look-ahead](#the-look-ahead).)

MASTER turned down (dist, dB re the output; a single dry note):

| MASTER | FM6 Classic / Clean | VA Classic / Clean | WHEEL Classic / Clean | CZ-1 Classic / Clean |
| --- | --- | --- | --- | --- |
| 0 dB | -48.4 / -76.2 | -49.6 / -89.3 | -43.4 / -89.0 | -44.2 / -89.8 |
| -12 dB | -46.5 / -75.8 | -60.1 / -88.9 | -59.5 / -88.5 | -60.1 / -89.4 |
| -24 dB | **-32.2** / -72.3 | -48.1 / -85.3 | -46.7 / -85.0 | -48.1 / -85.8 |
| -40 dB | **-15.6** / -58.6 | -31.5 / -71.6 | -31.0 / -71.3 | -32.1 / -72.1 |

Classic is cleanest about 12 .. 18 dB under full MASTER (the tanh falls away, the word length is not yet short) and
worse on both sides; Clean does not depend on the knob (what is left at -40 dB is half a unit of the 24-bit word, an
offset). On a loud chord the limiter's share stays whatever MASTER says: Clean's -41 .. -54 dB there at every setting,
where Classic at -18 dB reaches -59 dB. A trim of 0 dB instead of -2.5 would make single notes as loud as Classic and
chords 0.7 .. 2.4 LU louder, with the loud chords in gain reduction 67 .. 99 % of the time (dist -35 .. -41 dB): the
other end of the same trade.

### The look-ahead

Until this round Clean's limiter met a peak when it was already there: the gain eased down over 1.5 ms and the
samples before it had arrived went on into the knee. Now the chain's samples wait 32 samples in a delay and the gain
is down when the peak comes out.

**How** (`fx.c clean_block`). The audio ISR hands the chain 32 samples at a time (`CTL`). For each block:

1. each sample goes through the DC blocker into the delay and the sample that went in 32 earlier comes out; the
   block's peak is kept on the way (one pass, no second look at the delay);
2. the loudest of this block and the one before it (the one coming out) is the **window maximum**: two numbers, the
   kept peak of each block. The envelope takes it at once if it is higher, holds 40 ms (superseded by the third
   round: 200 ms, a hit's 40 ms, [The hold](#the-hold)), releases over 186 ms, as before, and is never let under the window maximum;
3. the block's gain is `CLN_T / envelope` (one divide, and only in a block where the envelope moved);
4. the samples coming out are multiplied by a gain that glides in a straight line from the last block's gain to this
   one's, a 32nd of the way a sample (kept at 32 times the gain's resolution: it ends exactly on the target, no
   step at a block's edge). Both ends of the line fit the block coming out under the threshold, so every point of it
   does: **no sample leaves the limiter over `CLN_T`**, from the first sample on, for any input
   (`tests/cr_output_test.c`: noise bursts, single full-level samples, gated squares and kicks up to 30 dB over,
   and the envelope against a brute-force maximum of the delay). A block in which the gain is 1 at both ends is
   copied out as it is.

One block of look-ahead and the linear glide between blocks are **Sloop 2.6's design** (`lim_block`; GPL-3.0,
credited in `fx.c` and `LICENSING.md`). No code was taken. ChoralRoot's own: the block's peak kept from block to
block instead of Sloop's scan of the whole delay every block (128 compares a block less), the hold and release of
the first round (40 ms, 186 ms, restarted within 0.3 dB; Sloop: 35 ms, 46 ms, 0.5 dB), the exact 32-bit product in
two halves instead of a 64-bit one, the exact glide without a divide, the mean over several blocks for a longer
look-ahead (`CLN_LA_K`), a copy loop while the limiter is idle, and the knee moved to the threshold.

**The length**, measured (the firmware's own chain built at each length, `-DCLN_LA_B` / `-DCLN_LA_K`; "top": the
residue above 8 kHz relative to the content there; "gain > 1 kHz": how much of the limiter's gain itself moves at
audio rate, dB re a gain of 1; the kick is the transient test below, 6 dB over):

| Look-ahead | VA chord: dist, top, gain > 1 kHz | WHEEL chord | CZ-1 chord | CZ-1, velocity 127 | kick: dist, the knee's share, gain > 1 kHz |
| --- | --- | --- | --- | --- | --- |
| none (before) | -53.6, -53.6, -94.5 | -41.1, -28.8, -81.6 | -43.4, -39.1, -84.2 | -41.0, -35.4, -81.8 | -24.5, **-33.8**, -80.1 |
| 16 (0.36 ms) | -52.7, -53.3, -77.6 | -40.0, -36.5, -64.4 | -42.2, -32.7, -68.5 | -39.9, -30.9, -64.5 | -22.8, -91.1, -63.2 |
| 24 (0.54 ms) | -52.8, -53.4, -82.4 | -40.1, -37.3, -69.2 | -42.2, -34.9, -72.9 | -39.9, -32.6, -69.4 | -22.8, -91.1, -68.2 |
| **32 (0.73 ms)** | -52.6, -53.4, -87.6 | -40.2, -37.9, -74.5 | -42.1, -37.9, -76.8 | -40.0, -35.7, -74.6 | -22.9, -91.1, -65.3 |
| 40 (0.91 ms) | -52.9, -53.6, -89.8 | -40.2, -37.9, -76.7 | -42.2, -37.6, -81.3 | -40.0, -35.5, -77.0 | -22.9, -91.0, -76.0 |
| 48 (1.09 ms) | -52.9, -53.6, -89.2 | -40.2, -38.0, -76.1 | -42.2, -38.6, -80.1 | -40.0, -36.7, -76.2 | -22.9, -91.1, -73.5 |
| 64 (1.45 ms) | -52.8, -53.6, -91.1 | -40.3, -38.7, -77.9 | -42.3, -39.8, -80.3 | -40.2, -37.8, -78.0 | -23.2, -91.0, -68.1 |

Read plainly:

- **Any length ends the overshoot**: the knee's share falls from -34 dB (the kick) and -63 .. -86 dB (the chords) to
  under -86 dB, and the peak sits at the threshold (-2.03 dBFS) instead of up to -0.1 dBFS.
- **The overall "dist" of a loud chord does not improve with any length** (it is 0.7 to 1.3 dB worse than before).
  That figure is not the attack: it is the limiter turning the level down by 0.1 to 2 dB each time the chord's
  beating brings a higher peak, counted because it happens within 50 ms. Whether the gain moves just before the peak
  or just after it is the same step. The first round's "what Clean has left is its 1.5 ms attack" was wrong in that:
  what is left is how often and how far the gain moves, which is the hold and the release (below).
- **The gain itself is less smooth than before**, at every length: a straight line over 0.7 ms has more above 1 kHz
  than the old 1.5 ms ease that was allowed to finish after the peak (7 dB more at 32 samples, 3.5 dB at 64). It is
  still 75 to 88 dB under the signal on the chords.
- Above 8 kHz the chords' residue stops improving at 32 samples (16 to 32: up to 5 dB; 32 to 64: 0.2 to 2 dB).

**32 samples, 0.73 ms**: the shortest at which the top no longer gains, exactly one audio block (the block's peak is
known before its first sample goes out, with no second buffer), and the cheapest. 64 is a build switch
(`-DCLN_LA_K=2u`: the gain 3.5 dB smoother, 1.45 ms); the listening files have it beside 32.

**The latency: 0.73 ms on everything Clean puts out** (Classic: none). Notes, the arp and the looper all pass through
it and stay where they were against each other. Two things do not pass through it and are now 0.73 ms **ahead** of
the sound in Clean: the metronome / count-in click (`cr_out.c cr_click_mix` is mixed after the master, as Felucca's:
never through the limiter) and, in a USB recording, the chord and bass pairs (taken before the master) against the
master pair. A key's own delay to its sound is a block (0.73 ms) to 2.9 ms already; nobody has judged the click on a
unit, and delaying it too would be 64 more samples of RAM and a second line: not done, named here. (Round 3: done,
both, [The click and the recording's stems](#the-click-and-the-recordings-stems): the click needs no line at all.)

**The transient test** (`tools/master_transient.py`: a kick every 500 ms, a sine falling from 160 to 48 Hz with
0.5 ms of attack and 3 ms of noise on it, on a held 55 Hz bass; its peak 6 or 12 dB over Clean's threshold; the same
mix through each chain):

| | Classic | Clean before | **Clean now** | Sloop 2.2's master | Sloop 2.6's master |
| --- | --- | --- | --- | --- | --- |
| kick 6 dB over: peak out, dBFS | -1.4 | -0.1 | **-2.0** | -2.6 | -5.2 |
| the knee's (tanh's) own share, dB re the output | -27.9 | -33.8 | **-91.1** | | |
| dist (with the gain's movement) | -20.6 | -24.5 | -22.9 | -19.7 | -19.8 |
| loudness, LKFS | -11.6 | -10.4 | -10.5 | -11.1 | -12.0 |
| kick 12 dB over: peak out | -0.3 | -0.1 | **-2.0** | -0.8 | -5.2 |
| the knee's share | -20.3 | -19.3 | **-88.8** | | |
| dist | -13.8 | -14.7 | -14.0 | -13.0 | -13.4 |
| the output's share above 2 kHz (the input's: -30.2 dB) | -27.2 | -26.2 | **-32.0** | -27.8 | -34.5 |
| loudness | -11.5 | -11.0 | -11.3 | -11.1 | -12.0 |

Before, a hit 12 dB over went through the knee at 11 % of the output (-19.3 dB) and came out with 4 dB more above
2 kHz than went in: a crunch on the hit. Now the hit is turned down as a whole and nothing is bent. The "dist" row
stays where it was: a kick 12 dB over a limiter is 12 dB of gain movement in any design.

**Before and after the look-ahead** (the first round's rows; full MASTER; Clean's loudness is unchanged to 0.02 LU
on every row but the kicks):

| Render | Clean before: GR share, dist, the knee's share | Clean now |
| --- | --- | --- |
| a single note, any of the four sounds | 0 %, -76 .. -90 dB | the same, bit for bit but 32 samples late |
| FM6 chord6 + bass, FX | 1.1 %, -94.7, -96.0 | 1.2 %, -93.9, -96.0 |
| FM6 chord6 + bass, FX, velocity 127 | 3.6 %, -79.4, -94.6 | 3.6 %, -77.0, -96.7 |
| VA chord6 + bass, dry | 0 %, -99.5 | the same |
| VA chord6 + bass, FX | 9.6 %, -53.6, -85.8 | 9.8 %, -52.6, -95.5 |
| WHEEL chord6 + bass, dry | 12.8 %, -46.7, -70.0 | 13.0 %, -46.0, -94.6 |
| WHEEL chord6 + bass, FX | 29.5 %, -41.1, -62.7 | 29.8 %, -40.2, -92.0 |
| CZ-1 chord6 + bass, dry | 2.1 %, -99.3, -99.3 | 2.1 %, -76.3, -99.9 |
| CZ-1 chord6 + bass, FX | 28.8 %, -43.4, -67.3 | 28.5 %, -42.1, -93.8 |
| CZ-1 chord6 + bass, FX, velocity 127 | 45.8 %, -41.0, -63.5 | 46.9 %, -40.0, -90.8 |

The distortion by band (dB under the band's own content; under 300 Hz / to 2 kHz / **2 .. 8 kHz** / above):

| | Clean before | Clean now |
| --- | --- | --- |
| FM6 chord6 + bass, FX | -101.2 / -89.7 / **-34.1** / -29.7 | -98.4 / -89.2 / **-34.1** / -29.7 |
| VA | -55.1 / -52.5 / **-54.5** / -53.6 | -53.3 / -52.0 / **-53.5** / -53.4 |
| WHEEL | -41.9 / -40.6 / **-40.5** / -28.8 | -41.9 / -39.0 / **-39.1** / -37.9 |
| CZ-1 | -43.4 / -43.4 / **-38.6** / -39.1 | -42.7 / -41.6 / **-40.8** / -37.9 |
| CZ-1, velocity 127 | -41.2 / -40.9 / **-37.4** / -35.4 | -40.6 / -39.5 / **-39.4** / -35.7 |

In 2 .. 8 kHz: CZ-1 2 dB better, VA and WHEEL 1 to 1.4 dB worse, FM6 the same. Above 8 kHz: WHEEL 9 dB better, the
others within 1.2 dB. On held chords the look-ahead is a small change either way; on attacks it is the difference
between bent and not bent.

**What would lower the chords' figure: a longer hold.** The gain moves because the envelope lets go between the
chord's beats. Measured with the look-ahead in (`-DCLN_HOLD`; the release as it is):

| Hold | VA: dist, loudness | WHEEL | CZ-1 | CZ-1, velocity 127 | kick 6 dB over: loudness |
| --- | --- | --- | --- | --- | --- |
| 40 ms (now) | -52.6, -10.93 | -40.2, -10.52 | -42.1, -11.17 | -40.0, -10.90 | -10.49 |
| 100 ms | -52.6, -10.97 | -41.5, -10.64 | -44.5, -11.28 | -44.3, -11.08 | -11.04 |
| 200 ms | -52.6, -11.01 | -42.7, -10.78 | -44.4, -11.42 | -44.7, -11.23 | -11.92 |
| 500 ms | -53.0, -11.08 | -45.5, -11.07 | -44.5, -11.60 | -45.4, -11.48 | -13.31 |

2 to 5 dB for 0.1 to 0.6 LU on chords, and a level that stays down longer after a loud hit (the kick's column). Not
changed: the brief was to keep the hold and the release, and it is a matter of taste with the listening files.
(Round 3: changed, with a short hold of its own for a hit so that the kick's column stays where it is:
[The hold](#the-hold).)

**Memory.** The delay is 32 frames of two int32 (the chain's Q17 samples: packing them into 16 bits would throw away
what Clean keeps): 256 bytes, in RAM (`.bss`, `cln_dl`), not in the pool. Both are the same on-chip RAM as far as the
project knows (the effects' lines, megabytes of traffic a second, are in the pool and `CR_FX_RAM` showed the code
cache was not their cost: docs/FX.md); the pool is the tight one (91.5 %), RAM has the room (82.1 -> 82.4 %).

**Switching Output.** The fade takes the chain's input to nothing over 16 blocks; then one more block of silence goes
through the old chain, so that Clean's delay plays out the last 32 samples of its fade (without it the end of the
fade, at 6 % of the level, would be cut off: a step); then both limiters are put at rest and the delay is emptied,
and the new chain fades in. 0.73 ms longer than before (24 ms in all). `cr_output_test.c`: no step above the steady
tone's own either way, the delay empty at the change, Clean's first block back silent.

**The make-up idea** (`CR_OUT_MAKEUP`, the DAC word's unused top 6 dB) is safer with the look-ahead in: Clean's peak
is now exactly the threshold, never the knee's range up to full scale, so a make-up of `M` puts the word's peak at
exactly `26000 M` and no higher: 5461 (the trim given back) peaks at -5.5 dB of the DAC's own full scale, where
before an overshoot could reach -3.5. Up to x 2.52 (+8 dB) fits the word. USB's master pair would still need its own
clamp above the Q15 sample. Not enabled.

### The cost on the unit

Counted in `build/choralroot.dis` (`python3 tools/fx_loops.py build/choralroot.dis master_block clean_block
clean_post17`, then the listing read for what a pass runs), at 205 instructions a microsecond, 128 frames a half:

| | instructions a frame | a half |
| --- | --- | --- |
| Classic (`master_block`, the smooth loop without LOWCUT), limiter idle | 76 | 47 us |
| Classic, limiting (a chord: all the time) | about 92 | 57 us |
| Clean before the look-ahead: `clean_block` 53 + `clean_post17` 18 + the word's low bits 12, limiter idle | 83 | 52 us |
| Clean before, limiting (holding) | about 99 | 62 us |
| **Clean now**, idle: `clean_block` 44 a frame (the DC blocker, the peak, in and out of the delay) + about 60 a block (2 a frame) + 18 + 12 | **76** | 47 us |
| **Clean now**, limiting: the same + 28 a frame (the glide, two exact products, two knee tests) | **about 104** | 65 us |

Idle is 7 instructions a frame cheaper than before the look-ahead (the envelope, its tests and the knee's are a
block's work now, not a sample's; a divide a block at most instead of one a sample while releasing); limiting is 5
dearer (the second pass over the block). Flash +96 bytes, RAM +256 (the delay), the pool unchanged. The emulator's
`perf.sh` reads 24 us for the master stage where it read 41 (an estimate with another compiler, not the unit's).

### Classic is 0.18's, bit for bit

(As of the second round. Since the third round's stereo chorus, an effect in both Output modes, this holds for the
output chain and for any sound without a chorus send: [What Classic's guarantee is now](#what-classics-guarantee-is-now).)

- `sh tools/emu/classic_exact.sh build/scratch/ref18` (a `git archive 19b6453` built beside): all 161 emulator
  scripts the 0.18 tree has, this tree's emulator with the Output held at Classic (`EMU_OUTPUT=classic`): 161 WAVs
  identical (104 of them with sound) when the chain was built. Since CZ-1's click fix (abb0b81, after it) the three
  scripts that play CZ-1 differ from 0.18 before any output chain: 158 identical, the three named. With the
  look-ahead in: the same 158, and **all 164 scripts identical to f402608's** (the tree before the look-ahead, both
  held at Classic: `REF_CLASSIC=1 sh tools/emu/classic_exact.sh build/scratch/ref_f402`).
- `tools/emu/test_cr.sh`: a script on a flash whose settings say Classic gives the same WAV as `EMU_OUTPUT=classic`,
  and not the default's.
- `tests/cr_output_test.c`: `out_master` in Classic is 0.18's mix line and `master_block`, sample for sample.
- `sh tests/run_tests.sh`: the 197 golden renders unchanged (Felucca's builds never select Clean).
- The harness's "chain: exact" for both chains: its own run of the chain equals the firmware's output.

### Proposed, not built in

- **The DAC word's upper 6 dB.** `CR_OUT_MAKEUP=1 ./build.sh` (`fx.c CLN_MAKEUP` 5461) gives Clean's 2.5 dB trim back
  after the knee, with MASTER, in the 24-bit word: a single note as loud as Classic, loud chords 3 .. 4 LU louder,
  the limiter as idle as in the tables above. The word then peaks 2.5 dB above anything ChoralRoot has sent
  (-5.5 dBFS of the DAC with the look-ahead: the limiter's ceiling times the make-up, exactly; up to -3.5 before it). fm1-x0x ships +6 dB as an option on the same hardware; nobody has heard this unit's
  headphone and speaker stages up there with ChoralRoot. Not the default, and not complete: with it USB's master pair
  (a Q15 sample) clips above full scale in USB Level = Master, and the SCOPE view's sample wraps (both to be handled
  if it is adopted). No register, rail or clock is involved; `OUT_SHIFT` and the HAL are untouched.
- **The HAL's comments** (`hal/fm1_audio.h`: "codec control lines", "24-bit left-justified") are wrong as found
  above; left as they are until the owner says the HAL may be touched for a comment.

## CZ-1 level (0.19, after the second round)

Round 2's note "a CZ-1 click remains in the mix" ([open item 10](#open-questions-and-proposals) below) and the level gap were CZ-1's own and are
closed in docs/CZ1.md ("Level", "The second round"). What matters to the output chain:

- **CZ-1 is 6 dB lower into the mix.** `cz_native.c` adds a voice `* 2` where it added `* 4` (a half-scale line, as
  `dsp.c` says of `<< 1`); every sample a voice leaves in its part's sum is exactly half. The same held 6-note chord
  at LEVEL 92: the part's median peak +2.4 -> -3.5 dB re full scale at MASTER's top, RMS -12.3 -> -18.0 dB (VA -3.0 /
  -15.6, FM TONE -3.8 / -16.8, FM6 -4.7 / -20.4); 40 -> 3 of 65 sounds peak above full scale. The three per-sound
  trims (BRASS 1, STRINGS 2, PIPE ORGAN 1) are gone. Every figure this document gives for a CZ-1 render ("CZ-1 BRASS
  1", `cz-*.wav` in `../audio-0.19/`) is of the old level: BRASS 1 moved by 1 dB only (its -5 dB trim against the
  engine's -6 dB), any other CZ-1 sound by 6 dB.
- **The limiter has less to do on CZ-1.** Over the 33 280 runs of `tests/cz_pops.c` (65 sounds, chords of four to six
  notes, with and without the CZ-1 bass, velocity 100 and 127): Classic's limiter worked in 54.9 % of the runs before
  and 6.3 % after, its lowest gain 0.28 -> 0.55. Clean's (`CZ_CLEAN=1`): 56.8 % -> 7.9 %, its lowest gain 0.26 -> 0.52. The runs
  left: INIT TONE most (313 of its 512), then SYNTH.BASS, HARP, SYNTH.LEAD 2, WHISTLE, BRASS 1, STRINGS 1; 48 of the 65
  sounds have at least one.
- **The click of item 10 is not a step.** The two `wavclicks.py` counts on BRASS 1 let go for 60 ms and struck again
  are the first bright window of each re-attack (its DCW is still open when its DCA comes back; the tone's own
  edges follow at five times that level for as long as the note sounds). Classic shows the same event at 10 times
  its neighbourhood, under wavclicks' threshold of 12: masked by the count, not removed by the tanh.
  `wavclicks.py --both-sides` (a burst must stand above the sound before it and the sound after it) counts 0 there and
  still finds a 30 % gain step put into the same file by hand. `cr_output_loud.txt`'s single strike still sits on
  Clean's ceiling (peak 25 899, the ceiling 25 898: `test_cr.sh`).
- **A real step upstream was fixed**: a sounding CZ-1 note struck again at another velocity (not reachable from the
  emulator's scripts, which strike at one velocity).
- Listening files, through Clean at the power-on MASTER: `../audio-0.19/cz-trim/`.

## FM TONE's level (0.23, 2026-10-16)

The owner after 0.22: FM TONE ("the new Digitone Engine") "is quite a bit louder than the other engines". ChoralRoot
has no per-sound trims of the kind Sloop's `preset_trim.h` holds (`cr_bank.c CB_TRIM` is empty on this unit since
CZ-1's -6 dB of 0.19, above): an engine's level is its own, set against the others'. Measured with
`tests/va_levels.c loud` (every sound of FM TONE, the VA, FM6, CZ-1 and P5) and `dnsnd` (the 256 Digitone factory
sounds through the device manager's `.dnsnd` import, `web/fm1dnsnd.js dnsndToQuad`): BS.1770-4 integrated loudness
(K-weighted, 400 ms blocks, the -70 LUFS and -10 LU gates; re the Q15 output's full scale) of what leaves the master
as a player hears it at power-on: Output Clean, MASTER as at power-on, the part at its default LEVEL 104, the sends at
0, FM TONE's Drive its sound's, velocity 100. Three phrases: **held**, a 6-note chord (D6/9maj7) held 6 s, read 0.3 ..
5.9 s; **phrase**, four 4-note chords struck a second apart and held 0.9 s (a MONO sound: a bass line); **notes**, the
chords' top notes alone, one voice (the limiter does not take part: the engine's own level). Each also at LEVEL 80,
read +12 dB, the level under the limiter ("lin" in the tool's lines).

Medians over the chord sounds (MONO ones left out), LUFS, held / phrase / notes; a11e0ca (0.22) and after:

| | 0.22 | after |
| --- | --- | --- |
| FM TONE, the 16 presets (13 chord sounds) | -18.8 / -20.0 / -25.8 | -18.8 / -20.0 / -25.9 |
| FM TONE INIT (LEVEL 110) | -15.9 / -15.4 / -16.1 | -16.0 / -16.1 / -22.1 |
| the 256 Digitone factory sounds, imported | -17.2 / -17.6 / -20.9 | -21.2 / -21.6 / -26.7 |
| .. of them Drive 0 (162) | -16.6 / -17.3 / -21.4 | -22.1 / -22.2 / -27.4 |
| .. Drive 40 .. 127 (64) | -17.4 / -17.8 / -19.2 | -19.6 / -19.9 / -22.6 |
| VA (21) | -16.2 / -17.7 / -23.9 | the same, bit for bit |
| FM6 (19) | -28.6 / -21.2 / -27.6 | the same |
| CZ-1 (65) | -27.8 / -22.7 / -28.1 | the same |
| P5 (116) | -24.5 / -23.5 / -29.0 | the same |

So the factory presets were not the loud ones (voiced at AMP LEVEL 62 .. 90 on FM TONE's squared law), the sounds at
the Digitone's own VOL (100 .. 127: INIT, every import) were, by 4 to 9 dB against the other engines' medians: the whole
engine at its natural levels, not a few sounds. Fixed at the engine's output scale (`eng_quad.c quad_render`: a voice
into its part `>> 12`, was `>> 11`), before the part's DRIVE (the platform's DIST, which every engine feeds: a scale
after it made a strongly driven FM TONE sound 6 dB quieter than any other engine's at the same Drive), with the 16
presets' LEVEL x 1.41 so that they stay where they were voiced. Never the master. The imports now sit inside the other
engines' spread (phrase -21.6 against -17.7 .. -23.5, notes -26.7 against -23.9 .. -29.0). Each preset:

| sound | LEVEL | held 6-note chord | phrase of 4-note chords | single notes |
| --- | --- | --- | --- | --- |
| EP | 72 -> 102 | -23.1 | -22.1 | -28.0 |
| BELL | 80 -> 113 | -27.0 | -20.2 | -25.8 -> -25.9 |
| BASS (MONO) | 110 -> 127 | -17.3 -> -20.8 | -19.3 -> -22.8 | -19.3 -> -22.8 |
| PLUCK | 72 -> 102 | (silent: decays) | -25.0 -> -24.9 | -31.0 |
| BRASS | 76 -> 107 | -17.0 | -19.4 -> -19.5 | -25.0 -> -25.1 |
| GLASS PAD | 88 -> 124 | -15.9 | -17.9 | -23.6 -> -23.7 |
| HOLLOW | 76 -> 107 | -14.6 | -14.5 | -20.3 -> -20.4 |
| SQUARE LEAD (MONO) | 110 -> 127 | -12.3 -> -15.8 | -16.1 -> -19.6 | -16.1 -> -19.6 |
| METAL | 70 -> 99 | -24.6 | -20.0 | -25.9 |
| WOBBLE (MONO) | 115 -> 127 | -18.1 -> -21.6 | -20.3 -> -23.6 | -20.3 -> -23.6 |
| CLAV | 90 -> 127 | (silent: decays) | -25.5 | -30.3 |
| STRINGS | 80 -> 113 | -16.3 | -17.4 | -23.3 -> -23.4 |
| MARIMBA | 78 -> 110 | -60.0 | -24.7 | -30.2 |
| DRONE | 74 -> 105 | -14.9 | -16.3 -> -16.2 | -21.2 -> -21.1 |
| FEEDBACK | 72 -> 102 | -18.8 | -20.4 | -26.6 |
| NOISE-ISH | 62 -> 88 | -18.3 | -19.1 | -25.0 -> -24.9 |

(PLUCK and CLAV have decayed before the held chord's window: no level there.) The three MONO sounds reach the top of
LEVEL at 127 and are 3.3 .. 3.5 dB quieter; SQUARE LEAD is still FM TONE's loudest. The rest: docs/QUAD.md "Level",
HISTORY 3y119.

## Round 3: the hold, the chorus in stereo, the click, MASTER at power-on

The owner's decisions of 2026-10-12, after hearing the first two rounds ("dry, little difference between Clean and
Classic; with very wet FX clean is a definite winner over classic, classic has more crackles and dropouts"): a longer
limiter hold in Clean ("probably good to do"), the chorus in stereo, the click lined up with Clean ("ok, do it"),
Felucca's fix for MASTER at power-on ("yes"). Classic stays as it is, for comparison; Clean is the default.

### The hold

**What was chosen.** The envelope is held **200 ms** (`CLN_HOLD`; it was 40) and held again by every block's peak
within **1.2 dB** of it (`CLN_REHOLD`: `env >> 3`; it was 0.3 dB). A **hit**, a peak more than 3.5 dB above the
envelope (`CLN_HIT`: `env >> 1`), keeps the **40 ms** of the first rounds (`CLN_HOLD_HIT`) until the envelope has
begun to let go; from then on the long hold applies again. An envelope that stands above a steady sound (a release
that stopped inside the 1.2 dB) creeps down to it (`CLN_CREEP`: a 1024th of the way for every block whose peak is in
the window, once 200 ms of such blocks have gone by without the top itself, a peak within 0.03 dB of the envelope):
a second or two (a tone 2 dB over after a hit: 0.83 dB above it after 1 s, 0.18 dB after 2.5 s), too slow to be a
step; a sound whose top comes back (a bass note's period, a chord's beat) keeps its gain exactly. The release is unchanged (186 ms).

**Why not simply a longer hold.** The second round's table said 200 to 500 ms takes 2 to 5 dB off; it also keeps
the level down after a loud hit for as long as the hold is. Measured on the same mixes through the firmware's own
chain built with each setting (`tests/master_measure.c chain`; the mixes: the loud chords of the earlier rounds at
velocity 100 and 127; the same sounds very wet: reverb and delay sends at 120, Decay 100, Feedback 105; made signals:
sixteenth notes at 120 and 180 BPM over a held bass, 6 dB over the threshold (`tools/master_rhythm.py`), the kick
test, and ONE hit 12 dB over on a steady pad). dist in dB; the loudness and the recovery against the first row:

| Hold, re-hold window | WHEEL v127 | CZ-1 v127 | WHEEL wet | CZ-1 wet | arp 120 | chords' loudness | kick 6 dB over, loudness | one 12 dB hit: back within 0.5 dB after, mean dip over the next second |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 40 ms, 0.3 dB (rounds 1 and 2) | -34.7 | -40.0 | -35.6 | -35.0 | -30.4 | | | 455 ms, -2.65 dB |
| 100 ms, 0.3 dB | -35.6 | -44.3 | -38.3 | -36.7 | -36.8 | -0.1 .. -0.5 LU | -0.5 LU | 514 ms, -3.35 dB |
| 200 ms, 0.3 dB | -39.3 | -44.7 | -42.0 | -37.8 | -40.9 | -0.1 .. -0.7 | -1.4 | 614 ms, -4.51 dB |
| 300 ms, 0.3 dB | -40.2 | -45.0 | -41.1 | -38.3 | -39.7 | -0.1 .. -1.0 | -2.2 | 715 ms, -5.68 dB |
| 500 ms, 0.3 dB | -41.5 | -45.4 | -45.2 | -38.7 | -38.4 | -0.2 .. -1.3 | -2.8 | 915 ms, -8.01 dB |
| 40 ms, 2.5 dB | -41.7 | -44.8 | -47.2 | -38.6 | -38.4 | -0.1 .. -0.8 | -0.5 | 482 ms, -2.97 dB |
| 200 ms and 40 ms after a hit, 0.3 dB | -39.3 | -44.7 | -42.0 | -37.8 | -40.9 | -0.1 .. -0.7 | -0.1 | 455 ms, -2.65 dB |
| **200 ms and 40 ms after a hit, 1.2 dB (chosen)** | **-40.5** | **-45.6** | **-46.4** | **-38.9** | **-42.4** | **-0.1 .. -1.0** | **-0.2** | **466 ms, -2.79 dB** |
| 300 ms and 40 ms after a hit, 1.2 dB | -40.7 | -46.0 | -44.2 | -39.0 | -45.6 | -0.2 .. -1.3 | -1.9 | 466 ms, -2.79 dB |
| 500 ms and 40 ms after a hit, 1.2 dB | -41.9 | -51.6 | -50.6 | -39.1 | -43.2 | -0.3 .. -1.6 | -2.6 | 466 ms, -2.79 dB |
| 200 ms and 40 ms after a hit, 2.5 dB | -47.9 | -57.3 | -51.2 | -39.3 | -52.1 | -0.2 .. -1.7 | -1.4 | 482 ms, -2.97 dB |

Read plainly:

- **A plain hold buys its 3 to 10 dB with the level after a hit**: at 300 ms a single hit 12 dB over leaves the pad
  5.7 dB down on average for the next second where 40 ms left 2.65, and a kick every half second is 2.2 LU quieter
  because the envelope never comes back between two of them.
- **The hit's own short hold takes that away**: with it a single hit is released as it was (466 ms against 455, the
  mean dip -2.79 dB against -2.65), and the kick 12 dB over comes out as before (-0.3 LU).
- **The window does as much as the hold**: 0.3 dB to 1.2 dB is worth 1 to 11 dB more on the wet passages and the
  arps for 0.1 to 0.3 LU. 2.5 dB is worth 6 to 12 dB more on some passages, for up to 1.7 LU and a kick every half
  second held down (-1.4 LU): the other end of the trade, not taken.
- **Above 200 ms a regular kick at 120 BPM is held down for good** (300 ms: -1.9 LU, the gain steady 5.9 dB down):
  not pumping, but quieter than the player set it. 200 ms lets go between the beats as before (-0.2 LU). Inference
  from how the 300 ms row comes about, not measured: above about 150 BPM a hit on every beat is held at 200 ms too
  (the envelope has let go less than 3.5 dB when the next one comes, so it is no longer a hit): steady.
- **The release** (93 ms, 372 ms beside the 186 ms): within 1 dB on the chords; 372 ms doubles a hit's recovery
  (837 ms). Left at 186 ms.

So: 200 ms and 1.2 dB, with the hit's 40 ms, is the setting that takes the most off the chords and the wet passages
**without** making a hit's recovery longer or a 120 BPM kick quieter. What it costs is 0.1 to 0.9 LU on the loudest
chords (with the bass they stay 0.4 to 1.3 LU louder than Classic, below) and 1 LU on the made arps.

**Before and after, every passage** (Clean, full MASTER; dist overall / in 2 .. 8 kHz relative to the content there;
the share of the samples in gain reduction; loudness):

| Passage | 40 ms, 0.3 dB | 0.19 (200 ms / 40 ms, 1.2 dB) |
| --- | --- | --- |
| VA chord6 + bass, FX | -52.6 / -53.5, 9.8 %, -10.93 LKFS | -52.5 / -53.4, 40.0 %, -11.06 |
| VA, velocity 127 | -40.6 / -40.8, 53.1 %, -10.08 | -41.8 / -42.5, 99.3 %, -10.67 |
| WHEEL chord6 + bass, FX | -40.2 / -39.1, 29.8 %, -10.52 | -44.3 / -43.8, 51.0 %, -10.88 |
| WHEEL, velocity 127 | -34.7 / -34.3, 78.2 %, -9.61 | -40.5 / -40.5, 98.2 %, -10.48 |
| CZ-1 chord6 + bass, FX | -42.1 / -40.8, 28.5 %, -11.17 | -44.5 / -46.6, 52.1 %, -11.48 |
| CZ-1, velocity 127 | -40.0 / -39.4, 46.9 %, -10.90 | -45.6 / -47.3, 80.5 %, -11.34 |
| FM6, velocity 127 | -77.0 / -35.1, 3.6 %, -14.34 | -76.5 / -35.0, 7.6 %, -14.45 |
| VA, very wet | -36.1 / -35.8, 95.1 %, -9.68 | -40.5 / -40.5, 95.1 %, -10.43 |
| WHEEL, very wet | -35.6 / -35.1, 88.5 %, -9.63 | -46.4 / -45.7, 93.0 %, -10.35 |
| CZ-1, very wet | -35.0 / -31.8, 98.5 %, -9.80 | -38.9 / -36.0, 100 %, -10.65 |
| CZ-1, very wet, velocity 127 | -34.3 / -32.1, 99.6 %, -9.80 | -38.9 / -37.4, 100 %, -10.63 |
| FM6, very wet | -54.6 / -32.3, 12.3 %, -13.09 | -66.5 / -41.3, 23.8 %, -13.38 |
| sixteenths at 120 BPM over a bass | -30.4 / -30.0, 91.4 %, -10.03 | -42.4 / -42.6, 96.1 %, -10.99 |
| sixteenths at 180 BPM over a bass | -32.5 / -31.9, 91.4 %, -9.81 | -47.2 / -47.1, 95.5 %, -10.65 |
| the kick 6 dB over | -22.9 / -13.0, 70.2 %, -10.49 | -22.9 / -13.1, 75.0 %, -10.70 |
| the kick 12 dB over | -14.0 / -2.0, 98.5 %, -11.27 | -14.0 / -2.4, 100 %, -11.56 |
| one hit 12 dB over on a pad | -17.6 / -23.8, 12.3 %, -15.22 | -17.5 / -23.7, 12.6 %, -15.26 |

The gain share goes up because that is what holding is: the limiter is "at work" (its gain under 1) for longer and
moves less while it is. On the very wet passages, where the owner hears Clean ahead of Classic, the residue falls 4
to 11 dB (WHEEL: -35.6 to -46.4). VA's default chord and FM6 were already under -50 dB and stay there.

**The harness's own rows again** (`tools/master_measure_run.sh`, the 26 renders: "chain: exact" on all), Clean
before and after the hold; the rows not listed are unchanged to the last digit (every single note, every dry FM6 and
VA chord: the limiter is idle on them):

| Render | Clean, rounds 1 and 2: GR share, dist, loudness | Clean, 0.19's hold | Clean - Classic now |
| --- | --- | --- | --- |
| FM6 chord6 + bass, FX | 1.2 %, -93.9 dB, -15.0 LKFS | 8.6 %, -86.1, -15.1 | -1.4 LU |
| FM6 chord6 + bass, FX, velocity 127 | 3.6 %, -77.0, -14.3 | 7.6 %, -76.5, -14.4 | -1.1 LU |
| VA chord6 + bass, FX | 9.8 %, -52.6, -10.9 | 40.0 %, -52.5, -11.1 | +1.1 LU |
| WHEEL chord6 + bass, dry | 13.0 %, -46.0, -10.9 | 37.5 %, -48.6, -11.1 | +0.6 LU |
| WHEEL chord6 + bass, FX | 29.8 %, -40.2, -10.5 | 51.0 %, -44.3, -10.9 | +0.9 LU |
| CZ-1 chord6 + bass, dry | 2.1 %, -76.3, -11.8 | 6.1 %, -76.6, -11.8 | +0.4 LU |
| CZ-1 chord6 + bass, FX | 28.5 %, -42.1, -11.2 | 52.1 %, -44.5, -11.5 | +1.1 LU |
| CZ-1 chord6 + bass, FX, velocity 127 | 46.9 %, -40.0, -10.9 | 80.5 %, -45.6, -11.3 | +1.3 LU |
| kick 6 dB over | 70.2 %, -22.9, -10.5 | 75.0 %, -22.9, -10.7 | +0.9 LU |
| kick 12 dB over | 98.5 %, -14.0, -11.3 | 100 %, -14.0, -11.6 | -0.1 LU |

(With the stereo chorus in, below, the FX rows' mixes are other mixes: VA -59.2 dB at 50.5 %, WHEEL -46.5, CZ-1
-45.2 and -46.2 at velocity 127, FM6 -96.0; the loudness within 0.1 LU.) The no-sample-over-the-threshold proof is
untouched: the envelope is still never under the loudest sample in the delay (the creep stops at it).

Tests: `tests/cr_output_test.c` (the hold's length to three blocks; a hit's 40 ms and its recovery under 520 ms; a
level that swings 1 dB every 150 ms keeps one gain; after a hit the envelope comes down to the level that follows
and is never turned down again on the way; the look-ahead's checks as before). Listening files:
`../audio-0.19/hold/` (`tools/master_listen_hold.py`: each passage before, after, and both in one file).

### The chorus, stereo

The chorus that runs on the unit was Felucca's in `fx.c` (`fx_buses`; with `CR_FX` 1 nothing else was left in
that function): one tap on a line of 2048 samples, 400 + 0 .. 6 x Depth samples back (9.1 .. 17.2 ms at the default
Depth 60) on a sine LFO, returned equal to both sides. Since 0.19 the unit's is `cr_fx.c xc_block`: **two taps on the
same line**, the left one Felucca's own, the right one the same LFO **a quarter turn ahead**. Rate, Depth and the
send mean what they did; the line, its memory and the pool are unchanged. An effect, not a part of the output stage:
it is the same in Classic and in Clean. docs/FX.md "The chorus, stereo" has the design, the cost, the Width
knob (0.21) and the glide under a turning Depth or Width (0.21).

**A quarter turn, not Sloop's half.** Computed from the firmware's own numbers (`tools/chorus_response.py`: dry at 1,
the wet at the send's gain, 100 Hz .. 10 kHz, every moment of the LFO's turn), and checked on the code
(`tests/cr_fx_test.c`):

| Send 45 (the default: the wet 9.0 dB under the dry) | worst notch / peak at any moment | the response averaged over the LFO's turn | band x time more than 3 dB down | the two ears' wet against each other | what is left of the motion |
| --- | --- | --- | --- | --- | --- |
| 0.18: one tap, both ears and the mono sum | -3.8 / +2.6 dB | -0.75 .. +1.16 dB | 16 % | 1.00 (the same signal) | 1.00 |
| 0.19, either ear | -3.8 / +2.6 dB | -0.75 .. +1.16 dB | 16 % | -0.01 | 1.00 |
| 0.19, the mono sum (L + R) / 2 | -3.8 / +2.6 dB | -1.09 .. +0.96 dB | 5 % | | 0.71 |
| half a turn apart (Sloop 2.6), the mono sum | -3.8 / +2.6 dB | -1.02 .. +0.98 dB | 5 % | +0.01 | **0.00** |

| Send 127 (the wet as loud as the dry) | worst notch / peak | averaged over the turn | more than 3 dB down |
| --- | --- | --- | --- |
| 0.18, both ears and the sum | -84 / +6.0 dB | +0.78 .. +4.00 dB | 23 % |
| 0.19, either ear | -84 / +6.0 dB | +0.78 .. +4.00 dB | 23 % |
| 0.19, the mono sum | -78 / +6.0 dB | -1.61 .. +3.25 dB | 18 % |
| half a turn apart, the mono sum | -73 / +6.0 dB | -0.95 .. +3.22 dB | 18 % |

- **Each ear has the comb it had** (it is one tap beside the dry sound: -3.8 / +2.6 dB at the default send, a full
  notch at a full send), but never the same one as the other ear: the two wets are unrelated over the band
  (correlation -0.01; measured on noise through the code: 0.005). That is the width.
- **The mono sum's deepest notch is 0.18's, no deeper**: it is reached at the two moments of a turn where the two
  taps cross. Everywhere else it is shallower: the part of the band and the time more than 3 dB down falls from 16 %
  to 5 %. Measured on the code with held tones: the sum's level between -3.72 and +2.63 dB of the dry (0.18: down
  to -3.81; the bound 1 - g: -3.80).
- **Half a turn apart is as wide and stands still in mono**: the two taps then move against each other, their mean
  does not move, and one speaker gets a comb that only breathes. A quarter turn leaves 71 % of the swing. The FM-1's
  own speaker is one speaker (whether it gets the left channel or the sum is not known from the material at hand:
  with the left alone it plays 0.18's chorus).
- **The mono sum is a little quieter** than 0.18's: two different taps add to less than one tap twice. 0.0 to 0.4 dB
  at the default send, 0.8 to 1.5 dB at send 105 (the listening files' render.log).

Two taps left and right on the one line and the LFO taken once a block with the read points a line across it are
**Sloop 2.6's design** (its `fx.c fx_buses`; GPL-3.0-only; credited in `cr_fx.c` and `LICENSING.md`). No code was
taken. ChoralRoot's own: the quarter turn and its width parameter (`xfp[XP_CWIDTH]`: 0 = both ears the left tap, 127
= a quarter; no knob yet), a line that ends exactly on the next block's first point, the return straight into the
mix, and no work at all while nothing is sent and the line is empty.

**Cost on the unit** (the listing, 205 instructions a microsecond, 128 frames a half): 0.18's loop 48 instructions
a sample with its sine, 30 us a half, always. 0.19: 52 a sample and about 110 a block for the four LFO points: 35
us a half while a part sends; the mix line lost its `+ wet` (3 a frame, 2 us): **+3 us a half with a chorus send**,
and **-29 us with none** (the look at the block: 3 us). Flash and RAM under "What it all costs" below.

Listening files: `../audio-0.19/chorus/` (`tools/chorus_listen.sh`: a note and a six-note chord on TINE EP and VA
LUSH PAD, 0.18's return and 0.19's, at the default send, at a high one and with all three effects, the mono sums,
and one file with the three after each other).

### The click and the recording's stems

Everything that is mixed after the master was looked for: it is **the click alone** (`cr_out.c cr_click_mix`: the
metronome and the count-in are the same generator; there are no UI sounds; previews are notes and go through the
parts). And one thing is taken before the master: the USB recording's CHORD and BASS pairs. Both led Clean's output
by the look-ahead, 32 samples.

- **The click is made late, not delayed**: it is a resonator started by `crl.click`, so in a block that went through
  Clean its start waits `CLN_LA` samples (`cr_ck_wait`). The same samples, 32 later, with no line and no memory, and
  never through the limiter's gain, as before (mixing it in front of the delay would have put it through the
  limiter; a ring of its own would have cost 64 bytes for the same samples). `tests/cr_align_test.c`: the click of a
  block and a sample put into that block's mix leave the DAC on the same sample in Classic (sample 96 of the test)
  and in Clean (128); Clean's click is Classic's, sample for sample, 32 later. `tools/emu/scripts/cr_click.txt`: the
  count-in's four clicks through Clean are those through Classic at a lag of 32, correlation 1.000, the same peak.
- **The stems are aligned** (the owner records them): in a block that went through Clean the two part pairs wait 32
  frames in a line of their own (`fx.c ua_parts_late`: 4 x int16 a frame moved as two words, 256 bytes of RAM, 14
  instructions a frame: **9 us a half, only while the computer records through Clean**). The line starts empty with
  each recording and each change of the chain, as the master's does: a recording's first 0.73 ms of the part pairs is
  silence. The test: the master pair against the CHORD pair of a dry note, lag 0 at correlation 1.0000 in both modes.
  Classic: no delay, none added. **Not run on a host or the unit** (the test stages the frames as `choralroot.c`
  does, without USB).
- **SCOPE** reads the block after the master and the click, as before: it shows what the DAC gets, the click now
  where it sounds. Nothing to change.

### MASTER at power-on

Felucca 1.1.5 (commit 276f72a, its issue #137): MASTER was 2048 (70 % of the knob's travel) from `felucca_init`
until the main loop's first read of the pot, about 430 ms after the audio had started, and the smoothed value then
glided from mid-scale an eighth of the way a frame. Its fix reads the pot before `audio_init` (`master.c
master_boot`) and follows it under the splash.

**What ChoralRoot did, traced**: `main.c` as Felucca's (2048, `audio_init`, 30 + 400 ms of splash). Nothing sounds
under the splash: the engine takes no key and no MIDI before `cr_out_init` (`cr_ready`), and there is no boot chime,
no restored chord (the session restores no sounding note) and no click. Then the loop's first pass read the pot (one
step of the smoothing, from mid-scale) and ran ChoralRoot's power-on, `cr_ui_init`, which **set MASTER to 2048
again** and opened the engine. From there MASTER glided to the pot over the next 20 to 30 frames, 0.3 to 0.5 s. So
on a unit: a key struck (or a MIDI note arriving) in the first half second after the splash sounded at up to the
default's level whatever the knob said: with the knob low, loud; with it at the top, 6 dB quiet and rising. (A key
already down at power-on is not a press and plays nothing: seen in the emulator. The 0.3 to 0.5 s is read from the
code, an eighth of the way a frame; nobody timed it on a unit.)

**The port**: `firmware/src/master.c` (Felucca's two functions; `main.c` calls `master_boot` before `audio_init`,
`master_poll` every 10 ms under the splash and in the loop). ChoralRoot's own part: `cr_out.c cr_out_init`, which
`cr_ui_init` calls right after its default and which opens the engine, puts the pot's level back
(`fx.c fx_master_now`), and Clean's eased MASTER (`cln_mq`, a ramp over a block) is set with it, so the first block
does not ramp from the default. No line of `cr_ui.c` changed. The emulator runs the same two functions on its own
pot (`EMU_MASTER=ADC` sets it at power-on).

Tests: `tests/cr_align_test.c` boots the firmware with the pot at 160, 512 and 1023 and strikes a key 2 ms later:
the first sounding block is within 0.1 dB of the pot's level and so is the note 100 ms on, in both chains; with
0.18's start put back (2048, the smoothing from mid-scale) the same check reads 18.7 dB over the pot's level in the
first block and 10.4 dB after 100 ms, and fails. `tools/emu/scripts/cr_boot_master.txt`: the pot at 160 against the
top: -32.2 dB in the first 10 ms and 200 ms later, both chains. **Not checked on the unit**: that its ADC answers
before the audio starts as Felucca's does (the code falls back to the old behaviour if three reads fail).

### What it all costs

Counted in `build/choralroot.dis` against 778a6ad's:

| | before | 0.19 |
| --- | --- | --- |
| the chorus, a part sending | 30 us a half | 35 us (and 2 us less in the mix line) |
| the chorus, nothing sent | 30 us | 3 us (and the same 2 less) |
| Clean's limiter | 44 a frame idle, 97 limiting (`clean_block`'s loops) | 44 and 93; about 20 instructions a block more for the hold (under 0.5 us a half) |
| the click | | 3 instructions a block |
| the recording's stems, Clean, while recording | | 9 us a half |
| flash (XIP) | 480 832 B | 481 396 B (+564) |
| RAM | 81 104 B | 81 300 B (+196: the stems' line 256, the mono wet's buffer gone) |
| POOL | 315 488 B (91.7 %) | unchanged |

`tools/emu/perf.sh` before and after (the emulator's estimate, for comparing builds only): the block's average within
1 % on every scenario (TINE EP chord 645 / 645 us, FM PAD + bass + loop + arp 944 / 945, VA 756 / 752, CZ-1 539 /
537, both sends at 127: 833 / 832), the fx stage -3 .. +5 us, the master 24 us in both, the same clicks counted by
`wavclicks` (1, 0, 0, 0, 0, 1, 4, 0, 0, 1, 0).

### What Classic's guarantee is now

- **Classic's output chain is 0.18's**, bit for bit, as it was: `tests/cr_output_test.c` (out_master in Classic = the
  mix line and `master_block`), and `tools/emu/classic_exact.sh` part 1: all 161 scripts the 0.18 tree has, this
  tree's emulator held at Classic **with the chorus's return held at 0.18's** (`EMU_CHORUS=mono`, a test switch of
  the emulator's build): 158 WAVs identical (101 of them with sound), the three CZ-1 scripts differ by CZ-1's own
  fix, as before. Against 778a6ad's own emulator, both held at Classic and this one at the mono return: every script
  identical, CZ-1's too.
- **With the stereo chorus as the unit has it, Classic is 0.18 bit for bit for any sound that sends nothing to the
  chorus** (the chord part's chorus send at 0, or FX off; the bass part's is 0 by default): part 2 of the script,
  `classic_dry_send0.txt` and `classic_dry_fxoff.txt`, the same file through 0.18's emulator and this one: identical.
- **With a chorus send** (the default is 45) Classic differs from 0.18 by the chorus's return and nothing else: the
  left channel's chorus tap is 0.18's to -45 dB, the right one is the new tap.
- Felucca's own builds (`CR_FX` 0) are untouched: the 197 golden renders, 0 changed.

### Not tested, and not done

- Nothing on the unit: the hold by ear on its speaker and headphones, the stereo chorus on its speaker (left or
  sum?), the click against a note by ear, a USB recording's stems in a DAW, MASTER at power-on with the real ADC.
- The browser emulator's test (no emcc on this machine): it builds the same `emu_fw.c` and `fx.c`, so its
  comparison with the native emulator holds by construction; its assertions (peak over 300, sound, the notes) do not
  depend on the chorus's width. (Superseded at the release: Emscripten 6.0.11 is in `~/emsdk`, not on the PATH;
  `tools/emu/web/build_web.sh` sources it, and the test, `node tools/emu/web/test_web_emu.mjs`, is run before a
  tag: docs/HANDOFF.md "Verification".)
- A Width knob: the parameter is there (`xfp[XP_CWIDTH]`), KNOB 3 of the Chorus page is free, the two lines in
  `cr_ui.c` and the one in `cr_settings.c` are in docs/FX.md; not added here (those files were another agent's).
  (Added in 0.21: docs/FX.md "The chorus, stereo".)
- docs/USB-AUDIO.md does not yet say that the part pairs are 0.73 ms late with the master pair in Clean.
  (Superseded: it does, under "ChoralRoot In: the channels", with what a change of Output does to a running
  recording's part pairs.)

## Open questions and proposals

1. **The level against Sloop** is the largest measured difference and 0.19's output chain does not touch it: part
   LEVEL 92 against 104, and trims. Raising LEVEL to 104 on 0.18 would have put the default chord 99 % of the time in
   Classic's limiter (dist -31.9 dB instead of -39.6); with Clean's headroom and the DAC word's 6 dB it becomes
   possible. A decision about the sounds' levels, by ear. (The owner, for 0.19: no per-sound loudness matching right
   now. Open.)
2. ~~The chorus's return is mono~~: stereo since the third round ([The chorus, stereo](#the-chorus-stereo)). Open:
   what the unit's own speaker gets. (The Width knob: KNOB 3 of the Chorus page since 0.21.)
3. **Output's default for the release** (Classic or Clean), the trim, and the make-up build: after listening.
   And since the second round: 32 or 64 samples of look-ahead. (The limiter's hold: chosen in the third round,
   [The hold](#the-hold); to be heard: `../audio-0.19/hold/`.) (The default: Clean, with Classic kept for
   comparison, the owner's third-round decision above. The make-up build is not finished:
   [Proposed](#proposed-not-built-in).)
4. **A control for LOWCUT / BASS+** (they are useful on the speaker): none added; the stage is held off.
5. **The reverb at 22.05 kHz** has no wet above 10 kHz (-12.9 dB against -4.3 dB): 0.18's cost decision
   (docs/FX.md), named here because it is a "top" difference to 0.17 and Sloop.
6. **The mix's own word length**: `mix_part_loop` shifts each part's sample by 2 before its level (`(b >> 2) * lvl
   >> 10`): the same class of loss as the master had, upstream of it, not measured here.
7. **The DAC chip** is unidentified. Its marking on the board would settle what filter it has.
8. ~~The click and the USB part pairs are 0.73 ms ahead of Clean's output~~: lined up in the third round.
9. ~~MASTER at power-on~~ (Felucca 1.1.5, #137): ported in the third round.
10. **CZ-1 struck again while it sounds** still shows a click to `tools/emu/wavclicks.py` in the mix of
    `BRASS 1` with the bass (2.30 s and 3.06 s of the script this round first used), before the output chain: in
    f402608's Clean as in this one, not in Classic (whose tanh rounds it). CZ-1's own item (docs/CZ1.md).
    (Superseded: followed to the sample and closed, [CZ-1 level](#cz-1-level-019-after-the-second-round): it is
    not a step, and Classic does not round it away, it only stays under the tool's threshold.)

## How to measure

```
sh tools/emu/build.sh                             # the generated headers
sh tools/master_measure_run.sh OUTDIR             # builds tests/master_measure.c, 26 renders (about 2 min)
sh tools/master_measure_tables.sh OUTDIR          # OUTDIR/table-all.txt, table-loudness.txt
python3 tools/master_listen.py OUTDIR WAVDIR      # the listening files and their README
sh tools/master_measure_run.sh OUTDIR -DCLN_TRIM=4096     # a variant: any of CLN_TRIM, CLN_T, CLN_HOLD, CLN_REL, CLN_MAKEUP,
                                                          # CLN_LA_K (blocks of look-ahead), CLN_LA_B (a block: 8, 16, 32)
python3 tools/master_transient.py OUTDIR                  # the transient test's mixes (the run script makes and uses them)
build/host/master_measure chain MIX.raw.i32 PREFIX        # any mix (int32 L R) through both chains
python3 tools/master_listen_la.py BEFORE NOW WAVDIR       # the look-ahead's listening files (two runs' OUTDIRs)
python3 tools/master_rhythm.py OUTDIR                     # round 3: arps over a bass, one 12 dB hit on a pad (mixes)
python3 tools/master_rhythm.py --gain X.clean.a00.i32 ..  # .. and the limiter's gain on them: recovery, dip, swing
build/host/master_measure render P --send 120,45,120 --decay 100 --fdbk 105   # a very wet setting
python3 tools/master_listen_hold.py BEFORE NOW WAVDIR     # the hold's listening files (two directories of chain records)
python3 tools/chorus_response.py                          # the chorus's comb: 0.18's tap, 0.19's two, Sloop's half turn
sh tools/chorus_listen.sh [WAVDIR]                        # the chorus's listening files (the emulator, EMU_CHORUS=mono)
cc -O2 -w -Ibuild/gen -Ifirmware/src -o build/host/va_levels tests/va_levels.c -lm
build/host/va_levels loud                                 # every sound's LUFS (FM TONE, VA, FM6, CZ-1, P5), the medians
build/host/va_levels dnsnd LIST                           # .. imported Digitone sounds (LIST: "name|blob hex|drive|pan")
```
(a variant of the hold: `-DCLN_HOLD=`, `-DCLN_HOLD_HIT=`, `-DCLN_HIT=`, `-DCLN_REHOLD=`, `-DCLN_CREEP=`; rounds 1 and
2's limiter is `-DCLN_HOLD=1764u -DCLN_HOLD_HIT=1764u -DCLN_REHOLD=5 -DCLN_CREEP=31`.)

`tests/master_measure.c` is the firmware as the emulator builds it, driven through its key matrix, with the mix
captured from `fx.c`'s buffers after every block; the captured mix is then run through the firmware's own chain
functions for each MASTER setting (Classic a frame at a time, Clean a block at a time as the ISR calls it, its gain
of each sample from `fx.c`'s `CLN_GAIN_TAP`; Clean's record holds the input 32 samples late, as its output is), which
gives the limiter's gain for every sample. Run at the
render's own MASTER this must equal what the firmware put out, to the bit ("chain: exact"; the script fails if not).
`tools/master_analyze.py` reads the held part:

- **GR%**, **GRavg**: the share of the samples with the limiter's gain under 1, and its mean.
- **dist**: the residual of `out - k * in * slow(g)` (the limiter's gain smoothed over 50 ms taken out: what a
  compressor may do inaudibly), dB re the output: the gain's fast movement, the clip's curve and the arithmetic
  (truncations, the word length at a low MASTER). The input is the mix times the gain with nothing lost (floating
  point). **It counts any level change faster than 50 ms as distortion**: a limiter stepping down 1 dB for a new
  peak shows here however cleanly it does it. **clip** (the table's column) is the part that bends the wave: the
  residual of `out - k * in * g` with the gain as it was; **g>1k** is the gain's own content above 1 kHz.
- **LK**: K-weighted loudness (BS.1770's two filters in the frequency domain, no gating), LKFS re the Q15 sample's
  full scale. An estimate.

The data of 2026-10-12: `../audio-0.19/` beside the repository (`table-all.txt`, `table-loudness.txt`, the WAVs and
their `README.txt`); the first measurements (0.18's master alone): `../muffled-review/`; the second round's listening
files (Classic, Clean before and with the look-ahead, 64 samples, the same mixes through Sloop 2.6's master, Sloop
2.6's own chords): `../audio-0.19/lookahead/` and its `README.txt`.

Tests: `tests/cr_output_test.c` (the knee, the gain, the staging, the look-ahead: the block maximum against brute
force, no sample over the threshold, the gain never above 1 and never a step, no DC shift, the delay exactly 32
samples, calls that are not whole blocks; MASTER last, the switch and nothing stale after it, Classic, USB;
`sh tests/run_cr_tests.sh`), `tools/emu/scripts/cr_output.txt`, `cr_output_classic.txt` and `cr_output_loud.txt`
(`tools/emu/test_cr.sh`: Clean is Classic's passage 32 samples late, `tools/emu/wavlag.py`; a loud chord's peak sits
on the limiter's ceiling), the seeded flash images of `tools/emu/test_persist.sh`, `tools/emu/classic_exact.sh`.

Round 3: the hold in `tests/cr_output_test.c`; the chorus in `tests/cr_fx_test.c`; `tests/cr_align_test.c` (the click,
the stems, MASTER at power-on); `cr_chorus.txt`, `cr_click.txt`, `cr_boot_master.txt` and the two `classic_dry_*.txt`.
