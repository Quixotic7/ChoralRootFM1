# The effects: a stereo delay and a large reverb (0.18)

ChoralRoot 0.18 replaces Felucca's mono tempo delay and its ROOM / SPRING reverb with a stereo delay in the spirit of
the Moog Matriarch's and a large, slow, modulated reverb. The drive is Felucca's, unchanged; the chorus was until 0.19,
which returns it in stereo (["The chorus, stereo"](#the-chorus-stereo-019)). This is the
design as built: what the code is, why, what it costs, and how to tune it. The player's side is
[MANUAL.md section 8](MANUAL.md#8-fx).

- the code: `firmware/src/cr_fx.c`, included by `fx.c` when `CR_FX` is 1 (`choralroot.c`, the emulator). With `CR_FX`
  0, the default, `fx.c` is Felucca's as it was: its own units, `tests/reverb_test.c` and the 197 golden renders of
  `tests/run_tests.sh` are untouched, and no golden was updated;
- the tests: `tests/cr_fx_test.c` (`sh tests/run_cr_tests.sh`), `tests/run_cr_fx_rates.sh` (half against full rate),
  `tools/emu/test_cr.sh` (the layer, "the effects and the sends are the instrument's"), `tools/emu/test_persist.sh`
  (the session), `tools/emu/perf.sh` scenario (l) and its "fx parts" line;
- the tools: `tools/fx_halfband.py` (the half-band filter's coefficients), `tools/emu/fx_review.py` (listening renders,
  old against new), `tools/fx_plots.py` (pictures of any WAV: level, spectrogram, Schroeder integral);
- on the unit: `fm1_install.py --debug` (SysEx 78 PEAK: "What the unit says").

**This is the second build.** The first (both effects at half the rate, every product rounded towards zero) was heard on
the unit and failed: "a lot of various noise or pops", "the delay just abruptly drops out even with the feedback turned
up". What was wrong and what changed is under "What the first build got wrong".

## The rule: the effects and the sends are the instrument's

The owner's decisions of 2026-10-11 and -12: "The FX settings themselves should be global and not change when changing
presets", then, after playing it: "I actually don't think we should save the sends with the presets ... thus I can switch
through the presets and experience the effect on each instrument."

| What | Where it lives | Changed by a sound load |
| --- | --- | --- |
| each **part's sends** into the reverb, the chorus and the delay, and its **drive** | one value a send (`cr_ui.c cu_send` / `cu_send_set`): the chord part's in `cs.fx_amt[]`, the bass part's in its track (`P_REV`, `P_CHOR`, `P_DLY`, `P_DIST` of `trk[]`), which no load writes (`cr_bank.c param_kept`, as Level and Pan). See "The sends: one value" below | **never** (but FM TONE's Drive, the sound's: "FM TONE's Drive" below) |
| every effect's **own parameters** (16 of the delay and the reverb: `cr_fx.c` `xfp[]`; the chorus's rate and depth: `song.g`) | global | never |
| all of it over a power-off | the session's FX block, by tag (`cr_settings.h`: `fx.amt[]` the chord part's sends, `CSS_FXP_BS_*` the bass part's, `CSS_FXP_*` the parameters; [SETTINGS.md](SETTINGS.md)) | |
| FX on / off | the settings block (`fx_on`) | no |

- **The FX layer's first page, Sends, is the chord part's four** (Drive, Chorus, Delay, Reverb on KNOB 1..4), and
  **"Send" on KNOB 4 of an effect's own page is the same send** (0..127, read in per cent wherever it is shown:
  "One read-out, one step" below). There is no master wet level: the returns are fixed (`XD_WET`, `XR_WET`), matched
  to the old effects (below).
- **The bass part's sends** are set in the bass sound's editor, FX group (BASS held + EDIT, then the FX button), and by
  CC 91 / 93 / 94 on the bass channel. Nothing on the main pages sets them, and nothing was added: the FX layer,
  its Sends page too, is the chord part's.
- **The editor's FX group** (`cr_pages.c`: Drive, Chorus, Delay, Reverb) shows and edits the part's sends: the chord
  sound's editor the chord part's (the same four values as the FX layer's Sends page and Send cells), the bass
  sound's the bass part's. A change there no longer marks the sound as edited. FM TONE's AMP page has a Drive
  cell too (`eng_quad.c QUAD_XDIST`): on FM TONE the drive is the SOUND's ("FM TONE's Drive" below).
- **A preset's or a saved sound's send fields** are ignored on load. A save writes the part's current sends into them,
  as it always wrote the part's values: the record's format is unchanged and an older firmware reads it as before.
- **Defaults** (a new unit, Options > Reset Settings): the chord part reverb 35, chorus 45, delay 25, drive 0 (what
  the power-on sound, FM6 TINE EP, was made with); the bass part reverb 10 (VA DEEP SUB's), the rest 0.
- **FX off closes every part's sends** (`fx.c` `fx_off`), the bass part's too, and the chord part's drive
  (`cu_fx_apply` writes 0 into its track), unless that part plays FM TONE.
- MIDI: CC 91 / 93 / 94 set a part's reverb / chorus / delay send; a program change does not touch them.
- A send is reached as a ramp over a block (`fx.c fx_sg`): turning Send, FX on / off and a CC write no step into
  what the delay and the reverb are fed.

### The sends: one value

The owner, 2026-10-12: "if I am in the FX menu from main and change the FX sends, then I enter the sound editor and go
to the FX send page, the send values shown [do not] update to what they actually are until I turn them."

The cause was two stores for the chord part's sends. The amounts live in `cs.fx_amt[]`; the track's `P_DIST .. P_REV`
are what the audio reads, and `cu_fx_apply` writes them: the amounts, or **0 while FX is off** (the drive has no
other gate: `fx.c` closes the three sends itself, the drive it reads from the track). The FX layer showed the first,
the editor the second. With FX on the two agree, so the editor followed the layer; with FX off (a tap of FX, in its
layer too) the editor's FX group read 0 % four times whatever the sends were, and its knob stepped from that 0: one
detent made the send 6 instead of amount + 6, switched FX on, and the other three jumped to their amounts. FM
TONE's Drive cell had the mirror fault: it wrote the track alone, so the FX layer did not show it and the next
`cu_fx_apply` took it back.

Now every reader and writer of a send in the UI goes through two functions (`cr_ui.c`):

| | chord part | bass part |
| --- | --- | --- |
| `cu_send(part, id)` | `cs.fx_amt[]` | the track's `p[id]` |
| `cu_send_set(part, id, v)` | `cs.fx_amt[]`, FX on, `cu_fx_apply` | the track's `p[id]` |

They are: the Sends page and the Send cells (`cu_fx_cells`, the `L_FX` knobs), KNOB 4 of the main view, CC 91 / 93 /
94, and the editor (`cr_edit.c ce_param` to read, `ce_knob` to write; `ce_send_ref` says which cell is a send: the
FX group's four, and FM TONE's Drive). The chord track's four fields are an output of `cu_fx_apply` and nothing
else: no screen reads them. So a send shows the same in both places at once, in both directions, FX on or off; with
FX off the editor shows the send that is kept (as it always did for the bass part) and the FX layer shows "off".
A send turned in either place switches FX on, as before. The audio is untouched: `cu_fx_apply` writes what it wrote.

Checked by `tools/emu/scripts/cr_fx_sends.txt` (`test_cr.sh`, "a part's send is one value"): the layer to the editor
and back, FX off, CC 91 on both channels, a sound change, FM TONE's Drive, the revert. The emulator's dump prints the
editor's FX group as its cells read it (`editor fx chord:` / `bass:`) beside the track's fields.

### One read-out, one step

The owner, 2026-10-13, to the note that the same send read "74" on the Reverb page and "75%" on the Sends page:
"Let's make the old per effect send parameters also show as 0-100% to match the first send page." Until then the
Send cells, KNOB 4's popup and the CC popups read `v * 99 / 127` (0..99) and the two knobs stepped 4, while the Sends
page and the editor read per cent of 127 and stepped 6. Now three functions of `cr_ui.c` are the only read-out and
the only step a send has:

| | |
| --- | --- |
| `cu_send_pct(v)` | `(v * 100 + 63) / 127`: per cent of the stored 0..127, rounded to the nearest (`params.c`'s `F_PCT`, what the editor always showed). 0 is 0 %, 127 is 100 %, and no other value reads either |
| `cu_send_text(v, ...)` | that number and `%`: "46%" |
| `cu_send_step(v, s, fine)` | `v + 6 s`, clamped to 0..127 (about 5 % a detent, the editor's `cp_dstep` for a 0..127 range); fine, the editor's SHIFT: `v + s`. Both ends are reached exactly, from anywhere |

| where | reads | steps |
| --- | --- | --- |
| the Sends page's four cells; the Send cell of Reverb 1, Chorus, Delay 1 and Drive (`cu_fx_cells`, the `L_FX` knobs) | `cu_send_text` ("off" with FX off) | `cu_send_step` |
| KNOB 4 of the main view (`EN_K4`) | the popup `cu_send_popup`: the number big over `%`, the meter 0..100 | `cu_send_step` |
| CC 91 / 93 / 94, either part | `cu_send_popup` (green the chord part's, orange the bass part's) | the CC's value |
| the editor's FX group, either part (`cr_edit.c ce_text`, `ce_knob`) | `cu_send_text` under its bar | `cu_send_step` |

None of these knobs has an acceleration (`cr_ui.c`'s knob handlers take the detents as they come): a detent is a
step in each place. The stored value, `cu_fx_apply` and the audio are untouched. Since the step is 6 of 127 and not
1 %, successive detents from a new unit's reverb send (35) read 28 %, 32 %, 37 %, 42 % .., and a send that has been
to an end comes back on another lattice (127, 121, 115).

Left as it is: FM TONE's `Drive` on its AMP page (`ce_send_ref`) is a number cell of that engine's
screen, beside Level and Pan, and reads the stored 0..127 as every cell there does; it steps with `cu_send_step`.
The owner, 2026-10-12, asked whether it should read per cent: "FM TONE Drive, no, keep as 127 to match Digitone."

Checked by `tools/emu/scripts/cr_fx_send_scale.txt` (`test_cr.sh`, "a send's read-out and its step are ONE"): the
reverb send a detent in each place and read in the next (KNOB 4's popup, the Sends page, Reverb 1's Send cell, the
editor, CC 91), 0 % and 100 % from each, the Send cells of Chorus, Delay 1 and Drive, the bass part's in its editor
and by CC. The dump's `screen reads:` line prints the last frame's popup and cells, as drawn.

### FM TONE's Drive

The owner, 2026-10-12: "Digitone presets should save and load the drive level with them and also always enable drive
if this is greater than 0 on FM TONE and FX is off." (His "Digitone presets" are FM TONE's.) So on FM TONE, and only
there, the drive is the sound's, as the Digitone's overdrive is its sound's.

**No stored format changed.** FM TONE's 80-byte patch (`eng_quad.c`, 'Q' version 3) has no drive value and still has
none: the AMP page's Drive column always was the part's `P_DIST` (`QUAD_XDIST`). The drive already had a place in
everything that stores a sound, and it was only not read back:

| | where the Drive is | before |
| --- | --- | --- |
| a factory preset | its row's `FX(drive, ..)` in `QUAD_PRESETS` (`preset_t.fx[0]`): **10 on WOBBLE, 0 on the other fifteen**. Our own values: the table is not converted from Digitone patches | not loaded since the sends became the part's |
| a user preset | its record's `P_DIST` field (`upreset.c up_rec_t`; `cr_bank.c cb_store` writes every track parameter) | written at every save, ignored at load |
| the session | `part[].p[P_DIST]` (`css_part_t`: the part's parameters as they are) | written, then overwritten by `cu_fx_apply` |

**The rule** (`cr_ui.c`, "FM TONE'S DRIVE IS ITS PRESET'S"): while a part's engine is FM TONE, its track's `P_DIST` is
the sound's Drive.

- **Loading** an FM TONE sound sets it (`cu_drive_load`, called by every load before it writes the engine: a factory
  preset's value, a user record's, INIT 0; the engine picker's switch to FM TONE loads 01 EP's, 0).
- **Saving** stores it: `cb_store` writes the track's `P_DIST`, which now is the Drive.
- **FX off does not close it**: `cu_fx_apply` leaves `P_DIST` alone for an FM TONE chord part and still writes 0 into
  the other three. (`fx.c` is untouched: it reads the drive from the track and never gated it; the bass part's drive
  was never closed by FX off, for any engine.)
- **One value on every screen**: `cu_send` / `cu_send_set` return and write the track's `P_DIST` for an FM TONE part,
  so the AMP page's cell (0..127), the Sends page's Drive, the Drive page's Send cell, KNOB 4 with Drive picked and
  the editor's FX group (all in per cent) are the same value, as before.
- **Turning it is an edit of the sound** (`cu_send_set` sets the part's `edited` and clears `trk[].user`), from any
  of those places; it does **not** switch FX on, since it sounds anyway.
- **With FX off the Drive cell of an FM TONE chord part stays live** on the Sends page and the Drive page (its per
  cent and its meter) while the other cells read "off": the smallest display that does not lie. The label still
  reads `fx · off` and the FX LED is off: they speak of the effects.
- **The part's own drive send is not touched**: it waits while the part is on FM TONE and is back when the part
  leaves it. The chord part's is `cs.fx_amt[]`, as ever (the session's `fx.amt[DRIVE]`). The bass part's send lives
  in its track, which FM TONE's Drive now occupies, so it is put aside in `cu_bdrive` (RAM) on the way in and
  restored on the way out; the session's tag 29 stores it from wherever it is (`cu_bdrive_p`). So a preset with
  Drive does not leave its drive on the next FM6 or VA sound, and no new stored field was needed.
- **The other engines: nothing changed.** Their drive is the part's send, not stored with presets, gated by FX off
  on the chord part.

**The session** comes back as it sounded when saved: `css_part_put` restores each part's parameters, FM TONE's Drive
among them (an edited one that was saved to no preset too), `cu_fx_apply` then leaves it, and the parts' sends go
back beside them (`fx.amt[]`, tag 29 into `cu_bdrive` while the bass part is on FM TONE). The order in `css_apply`
is fixed: the parts, the tags, the amounts, `cu_fx_apply`. GLO + SAVE (the revert) and Reset Settings run the same
path after `crs_reinit`, whose default loads take the parts off FM TONE first.

**Older records.**

- A user preset saved by 0.14 .. 0.17 or by an earlier 0.18 build loads unchanged, and its Drive is the byte that
  firmware left in `P_DIST`: **the part's drive when it was saved** (0.14 .. 0.17: what the preset then sounded
  with; an earlier 0.18 build: the part's drive send of that moment, whichever sound it had been set for). On the
  chord part that byte is **0 if FX was off at the save**, whatever the Drive cell showed. Nobody can tell from here
  which of the owner's FM TONE presets carry a drive above 0: the first load shows it in the AMP page's cell.
- A session written before this change: an FM TONE chord part comes up with the `P_DIST` its record holds, which is
  what sounded when it was saved (the amount, or 0 with FX off); the bass part's tag 29 and its `P_DIST` held the
  same value, which becomes both the sound's Drive and the part's send.
- Loop slots and backups reference no drive: nothing changed in them.

Checked by `tools/emu/scripts/cr_quad_drive.txt` and the four `cr_quad_drive_wav_*.txt` (`test_cr.sh`: the presets'
Drives, edit / save / away / back, FX off on the Sends page, the engine switched away and back, the bass part, the
picker's cancel; heard: FM TONE with Drive 127 against 0 with FX off, and FM6's two renders identical),
`quad_drive_set.txt` / `quad_drive_check.txt` (`test_persist.sh`: the session over a relaunch, the revert, the
parts' sends after it, Reset Settings) and `tests/cr_fm6_test.c` ("drive:": the sixteen presets, the store round
trip, an older record, an older session). The dump's `drive:` line prints each part's track value and its send.

## What the first build got wrong

Proved on the host before anything was changed; each has a test now.

1. **The delay dropped out** (`cr_fx_test` "no drop-out"). Every product in its loop was rounded towards zero, and the
   loop's saturator was `dsp.c`'s tanh table, whose first step is 255 / 256. Each pass round the loop therefore took
   one to three units off every sample whatever the feedback: a tail fell in a straight line instead of dying away. At
   a short time that is fast: at 21 ms, 47 passes a second, feedback at its top, a repeat was at -44 dBFS after two
   seconds and **gone half a second later** (measured on the first build: -44.5, -55.2, -111.5 dBFS in three
   consecutive 250 ms windows). Now: every product inside a loop **keeps its remainder** for the next sample
   (`xf_rs`: first-order error feedback, as `fx.c`'s `dc_block` does), and the saturators are exactly 1 below their
   knee. The same burst now falls 2.6 dB every quarter second, the same number in every window, down to -80 dBFS.
2. **Saves cut holes into tails.** The session is saved after every change, when the output is quiet; so that a long
   reverb would not block that for ever I had let the save go ahead after 30 s. A flash erase stops the audio for
   46 ms: every knob turned in the FX layer under a ringing tail ended in a hole half a minute later. That rule is
   gone: no settings or session write while the output is above -60 dBFS, with no time limit (`cr_settings.c
   crs_sounding`). `flash_holes` in `--debug` counts erases into a sounding output.
3. **Quiet tails were grainy and the frozen reverb leaked.** The same rounding, in the reverb: "Inf" from a moderate
   send was a straight line to silence. Now the lines keep their remainders too.
4. **The delay was dull.** At half the rate its repeats stopped at 9.5 kHz beside a dry sound that does not. The delay
   now runs at the full rate (below).
5. **The cost on the unit was far above the estimate** (below: "What the unit says").

And the second build, on the unit ("The cost on the unit", "Late is not slow"):

6. **The reverb cost three times its estimate** (331 .. 418 us a half for 116), the delay 170 .. 270 for 121. The
   loops were written a sample at a time, eight lines wide; this host's compiler runs that four lines at once, the
   unit's one after the other, reloading its states from memory in every sample.
7. **Every late half was a pop.** The audio ISR cleared the half's pending bit after its render. A render that ended
   after the boundary cleared the next boundary's interrupt with it: nobody rendered the next half, and the DAC
   played the half from two halves before again. That is the "buffer that instantly reset". A half was late not
   because it was slow but because the ISR started late: a flash read or a page program holds every interrupt for
   about a millisecond, and CZ-1's chords left less than that.

## The rates: the delay at 44.1 kHz, the reverb at 22.05 kHz

`CR_FX_DELAY_DS` and `CR_FX_REVERB_DS` (1 full, 2 half; `./build.sh` takes them from the environment) choose each
effect's rate. `sh tests/run_cr_fx_rates.sh` builds the unit test both ways, runs every check on each, and writes the
same material through both: `build/fx-review/cmp_delay_full.wav` / `_half.wav`, `cmp_reverb_full.wav` / `_half.wav`
and the impulse responses, with their spectrograms.

| | Half rate | Full rate |
| --- | --- | --- |
| the delay's band (Tone open) | flat to 9.5 kHz, -63 dB at 12 kHz | -0.2 dB at 9 kHz, -0.5 dB at 15 kHz, -0.6 dB at 18 kHz |
| the delay's memory for 0.74 s a side | 65 KB (or 1.49 s in 131 KB) | **131 072 B** |
| the delay's cost (emulator estimate, a half of 128 frames) | 92 us | **117 us** |
| the reverb's band | to 9.5 kHz | to 20 kHz |
| the reverb's memory | **40 960 B** | 81 920 B |
| the reverb's cost | **153 us** | 302 us |
| both | 222 us (7.6 %) | 417 us (14.4 %) |
| **as built: delay full, reverb half** | | **267 us (9.2 %)** |

**Since 0.22: both at half** (the owner, 2026-10-14, after hearing `build/choralroot-delayhalf.fwsc` on the unit:
"Delay sounded good at half rate"; `cr_fx.c CR_FX_DELAY_DS` 2, the line halved with it, "Memory at the full rate"
below: POOL 72.7 % for 91.7 % on that branch, 69.4 % for 88.5 % on the 0.22 line after the merge of 3y76, where
the reverb still runs on core 1 with Dual Core: DUALCORE.md "What each core runs"; `CR_FX_DELAY_DS=1 ./build.sh` is
0.18 .. 0.21's delay). What follows is 0.18's
reasoning for the full rate, kept as the record of why it was chosen then.

**As built in 0.18: the delay at the full rate, the reverb at half.** The delay's repeats are heard dry and bright beside the
note that made them, and the full rate costs it only 25 us on the estimate (it loses its down- and up-sampler); the
reverb is a dark wash whose top is its own damping, and the full rate would double it (+149 us) and leave 0.37 s of
delay in the memory that is there.

**Memory at the full rate.** A second a side is 176 KB; the pool has 131 KB for the delay with 17.7 KB left free, so
the line is 32768 samples: **0.743 s a side** (a 1/4 at 81 BPM and up; a division longer than the line plays at half).
The owner accepted "or lower if need be". At half rate the same memory is 1.49 s. **Since 2026-10-14 the line
follows the rate** (`XD_N` = 32768 / `CR_FX_DELAY_DS`): `CR_FX_DELAY_DS=2 ./build.sh` keeps 0.743 s in half the
samples and gives 65 KB of POOL back (the owner: "I'd try storing the delay at half rate, but I might not like the
sound, so it should be easy to go back": it is a build, `build/choralroot-delayhalf.fwsc`, and the release build is
the way back); `XD_N=32768 CR_FX_DELAY_DS=2` is the 1.49 s line in the same memory. What the half rate costs the
sound: the repeats lose what is above 11 kHz, as the reverb does (the half-band filters, "The rates" above).

### What the unit says, and the formula

The emulator counts host instructions and calls 259 of them a microsecond. On the owner's unit, with the first build
and Dual Core On, SysEx 73 DEBUG read `max_us 2580` of 2902 (89 %) and `late 12`: the emulator's worst half for that
build was 51 %. The estimate was at least 1.7 times too low on the worst half, and nothing said which stage.

`fm1_install.py --debug` now reads **SysEx 78 PEAK** as well (a command of its own: 73's count is one 7-bit byte and
its 127 values fill it). Its values are the worst cases **since the last read** (reading clears them: play, read, and
the numbers are that stretch's):

| Name | |
| --- | --- |
| `pk_block_us` | the largest whole half (the budget is 2902) |
| `pk_tick_us` `pk_part0_us` `pk_part1_us` `pk_fx_us` `pk_master_us` `pk_wait_us` | each stage's largest half |
| `pk_late`, `pk_late_stage`, `pk_late_us` | late halves; the latest one's largest stage (1 tick .. 6 wait) and its time |
| `pk_halves`, `fx_busy_halves` | halves counted; halves in which the effects ran |
| `fx_down_avg_us` / `_max_us`, `fx_delay_*`, `fx_reverb_*`, `fx_up_*` | inside the fx stage, over `fx_busy_halves`: the sends' down-sampler, the delay, the reverb, the wet's up-sampler |
| `flash_erases`, `flash_holes`, `saves_settings`, `saves_session` | since power-on: erases (17.8 ms measured on the unit, the interrupts masked and the buffer silenced; the emulator's model and the data sheet: 45), erases into an output above -60 dBFS (the rule: 0), saves |
| `stale_halves` | **the audible ones**: halves in which the DAC played something old (a boundary nobody served, or a half finished after the DAC had reached it). The rule: 0 |
| `pk_lag_us` | the furthest the ISR started behind its boundary (an estimate: "Late is not slow") |
| `pk_wall_us`, `pk_t5_us` | the longest half by the clock, TIMER5 nested in it (`pk_block_us` leaves TIMER5 out), and the most TIMER5 took of one half |
| `irq_gap_us`, `irq_gap_pc` | TIMER5 comes every 100 us: its longest gap (the interrupts were masked for about that, less 100) and the address it came back to after it (`build/choralroot.dis` names the section) |
| `key_ons`, `key_pick_us`, `key_eng_avg_us`, `key_eng_us` | the keys' way to the engine (docs/INTEGRATION.md "Keys while a frame draws") |
| `shed_voices`, `shed_last_voice`, `shed_last_why`, `shed_over_halves`, `shed_last_us`, `voices_faded`, `voices_stolen` | 3y44: what the overload shed did (docs/PROPHET.md "The crackle"). `shed_last_why` 4 (0.22): a voice of the bass part's P5 stack, taken before any chord voice |
| `p5_core0_voices`, `p5_core1_voices` | 0.22 (3y46, docs/PROPHET.md "Voices"): the most P5 voices core 0 and core 1 rendered in one block. The rule is four each; core 1's are the bass part's P5 voices and its share of the chord part's. With one core everything is core 0's (at most four) |
| `p5_bass_voices`, `p5_bass_given_up` | the bass part's most P5 voices (four with two cores, one with one), and held bass voices given up to the chord part (the chord comes first) |
| `erases_auto_sounding` | 3y48, since power-on (never cleared): automatic erases (the Options' or the favourites' poll) that started while something sounded in the second before. The rule is 0 |
| `last_erase_why`, `last_erase_obj`, `last_erase_ms` | since power-on: the latest erase's writer (0 other: a sound saved, a store; 1 the Options' poll; 2 the favourites' poll; 3 the global save or Reset Settings; 4 the calibration or the update gesture; 5 a backup restore; 6 a loop slot), its object (storage.c `OBJ_*`, 0x100 + k a loop slot) and when (ms since power-on, 0 none) |
| `irq_gap_ms`, `ua_arm_gap_max_ms`, `ua_cap_reset_ms`, `uptime_ms` | when `irq_gap_us` happened (a peak, cleared with it), when `ua_arm_gap_max_us` did (since power-on), the latest USB capture under- or overrun (the stream restarted: a gap the host hears), and the unit's clock at the read. A stutter in a recording with none of them near its time is not the device's |
| `pk_c0_busy_us`, `pk_c1_busy_us` | 3y50, **each core's half** (docs/DUALCORE.md "What each core runs"): the largest busy time of core 0 (its stages but `wait`, without core 1's job) and of core 1 (its voices' job and the reverb's), each in its own worst half |
| `pk_c0_wait_us`, `pk_fx_join_us`, `pk_c1_idle_us` | core 0's waits in one half (both joins: the same as `pk_wait_us`), of them the effects' join alone, and core 1 idle between its voices' end and the reverb's start |
| `pk_fx_map`, `c1_fx_jobs` | which core ran which effect in the largest whole half (`pk_wall_us`'s): 1 the chorus, 2 the delay, 4 the reverb ran, 8 the reverb on core 1 (so 15: all three, the reverb on core 1; 7: all three on core 0); and the reverb jobs core 1 ran |
| `saves_auto`, `erases_ahead`, `erases_near`, `flash_prog_us` | **the quiet writes** (values 61..64, 3y52: p5-g's 35..38 appended by name; 0.22, docs/SETTINGS.md "When flash is written"), since power-on: the saves the unit did by itself (an Option's, a favourite's); the erases done ahead (the favourites' and the session's, not the settings': at power-on before the audio starts, then after 10 s without a sound or an input); the erases the unit started by itself while a voice was active or within 2 s of an input (**the rule: 0**; SAVE held, a sound's or a loop's save are the player's and are not counted); the longest page program in us (the driver's wait for the chip, the interrupts masked; the page's SPI transfer before it is not in it) |
| `ui_frame_max_us`, `ui_frame_avg_us`, `flash_reads_ps`, `flash_read_bytes_ps` | **the UI and the flash reads** (values 65..68, 3y60; docs/PRESETS.md "What browsing reads"), since the last read: a UI frame's own time (input, LEDs, draw; `audio.c ui_ft`, from `main.c`'s loop) the longest and the average in us, without the wait to the next frame; the flash reads (`storage_hw.c st_read`) per second, calls and bytes. Turning PRESETS through a full pool reads a sound's record and patch per load (2 reads, ~270 bytes); 0d227e0 read ~5500 records a detent |

With Dual Core On **`pk_part1_us` is core 1's whole job of a half**: the bass part and, since 0.22, the chord part's
P5 voices that core 1 renders; `pk_part0_us` is core 0's own render (its share of the chord's voices), `pk_wait_us`
how long core 0 then waited for core 1. The half is the tick + the longer of the two + the effects + the master.
Since 3y50 the reverb runs on core 1 while core 0 runs the chorus and the delay: `pk_fx_us` is core 0's side of the
effects (the sends, the chorus, the delay, the sum), `fx_reverb_*`, `fx_down_*` and `fx_up_*` are measured on core 1
when it ran them (and added to the fx parts after the join), and `pk_wait_us` now holds both joins: the effects' join
alone is `pk_fx_join_us`. The emulator's core 1 runs a job when it is handed (inline), so its `fx` stage still holds
the reverb: perf.sh's figures do not show the overlap (estimates of the work, not of the unit's timing).

The instrumentation is five TIMER4 reads a block. `tools/emu/perf.sh` prints the same four parts ("fx parts"), so the
two compare line by line. The emulator's, for (l), the heaviest sound with both sends at 127:

```
fx parts (device us per half, avg / max): down 12 / 123, delay 121 / 209, reverb 116 / 237, up 22 / 67
```

### The cost on the unit

**The unit runs about 205 of its own instructions a microsecond**, and needs about 1.6 of them where this host needs
one. Measured, not reasoned: `xf_down` and `xf_up` are straight loops of 65 and 57 instructions a pass
(`python3 tools/fx_loops.py build/choralroot.dis xf_up xf_down`), 4160 and 7296 instructions a half, and the unit read
20 and 36 us for them (`fx_down_avg_us`, `fx_up_avg_us`): 208 and 203 a microsecond. The reverb's per-sample loop ran
about 1050 instructions a pass on the unit (415 in the listing, most of them eight times): 67 000 a half, 331 us.

So the emulator's "259 host instructions a microsecond" was low by a different factor for every stage, and worst
where the host's compiler turned eight lines into two vector operations:

| Stage (second build) | emulator, us a half | the unit, avg / max | factor |
| --- | --- | --- | --- |
| down | 12 | 20 / 47 | 1.7 |
| delay | 121 | 168 / 273 (running) | 1.4 |
| reverb | 116 | 331 .. 345 / 418 | 2.9 |
| up | 22 | 36 / 63 | 1.6 |
| CZ-1's chord, part 0 (perf.sh (g): 446 / 864) | | 1699 .. 1985 at its largest | 2.2 |

`CR_FX_RAM=1` changed nothing on the unit (reverb 345 for 331, delay the same): the code cache was not it. It stays a
build switch, off. (Not the whole story: the P5 engine's 3.3 KB sample loop did gain from RAM, a quarter of the
part's time: [Code in RAM](#code-in-ram-3y41).)

**The cost model that holds**: count the unit's own instructions. `tools/fx_loops.py` lists a function's loops from
the build's disassembly; times each loop's passes a half, over 205, is its time. The third build's loops, for the
defaults (wow and Mod on, nothing gliding), a pass being one sample:

| Loop | instructions a pass | passes a half | us |
| --- | --- | --- | --- |
| `xf_down` | 65 | 64 | 20 |
| delay: a side's read and filters, drifting (at rest 17, gliding 40) | 27 x 2 | 128 | |
| delay: feedback, saturators, writes, the wet | ~57 (87 with both knees) | 128 | |
| **delay** | **~111** | | **~70** (at rest ~57, gliding or crossing modes ~85 .. 115) |
| reverb: low cut 30, pre-delay 4, four diffusers 21 + 21 + 21 + 27 | 124 | 64 | |
| reverb: the lines' reads, four at rest 5 and four swinging 14 | 76 | 64 | |
| reverb: the output 29, the lines' low-pass and gain 8 x 11, the mix 51 | 168 | 64 | |
| reverb: the lines' writes 8 x 15 | 120 | 64 | |
| **reverb** (and ~25 a pass of loop set-up) | **~513** | | **~160** |
| `xf_up`, two sides | 49 x 2 | 64 | 31 |
| `xf_block` itself (the parameters' hash, clearing and looking at the half-rate block) | | | ~8 |
| **both effects** | | | **~290** (second build on the unit: ~560 .. 600) |

These are counts read off the listing, good to about a tenth; **the unit's `fx_*` will say**. It said (2026-10-14,
`tools/perf_unit.py`, docs/INTEGRATION.md "The fx stage's parts and TIMER5"): down 20 .. 21, delay 90 .. 108,
reverb 193 .. 198, up 34 .. 37 on average with an 8-note chord and the bass, 340 .. 385 together: the count times
1.2 .. 1.3, the reverb 40 % of a stage of 445 .. 535. The targets were the
reverb under 150, the delay under 100, the stage's four parts under 300: the delay meets its own, the total is at
it, **the eight-line reverb is about 10 us over**. What would take it under and was not done because it changes the
sound: two diffusers instead of four (-42 a pass), two swinging lines instead of four (-18). That is
**`CR_FX_LITE=1 ./build.sh`**: ~450 a pass, ~140 us, the same lines, lengths, decay and level (the checks all pass;
its tail is a little less smooth in the first 50 ms and the stereo image moves less). `build/fx-review/*_lite.wav`
beside the others: both can be heard, on the unit too.

**What was changed, and what it cost in sound** (`python3 tools/fx_cmp.py build/fx-review/round2 build/fx-review`:
level, residual, third-octave spectrum of the second build's renders against the third's):

| Render | level | residual | widest third-octave gap |
| --- | --- | --- | --- |
| `cmp_delay` (a bright stab, the defaults) | -0.01 dB | -43.5 dB | 0.29 dB at 3.2 kHz |
| `ir_delay_pingpong` | -0.01 dB | -47.1 dB | 0.23 dB at 4 kHz |
| `cmp_reverb` | 0.00 dB | -38.4 dB | 0.43 dB at 4 kHz |
| `ir_reverb_default`, `ir_reverb_large` | 0.00 dB | -37 dB, -34 dB | 0.18 dB, 0.06 dB |

- **Block-wise, a stage and a line at a time**: each loop has its few states in registers for the whole block.
  Every line and every diffuser is longer than a block, so a block's reads never see its own writes: the samples are
  the per-sample loop's. With Wow 0 the delay is **bit for bit** the second build's; the reverb differs by at most 12
  units of 32768 (its two gains a line are one multiply now, one rounding instead of two).
- **The rings are walked in straight pieces**: a pointer and an index, split where the ring wraps, instead of a mask
  in every sample. `XR_BASE` and `XR_APB` moved to multiples of a block (16 half-rate samples) so that a block's
  writes never wrap, and a block apart so that no stage reads what the stage before it wrote in the same block.
- **A line at rest reads its samples**; one that drifts (the wow; MOD: far under a sample a block) reads between the
  same two samples all block with the fraction a ramp; only a glide (Time, Size) pays for the general read.
- **The glides and the swings are ramps over a block** (the per-sample lag became its block's step): the same times,
  no step in the read position.
- **The delay's moving read is linear** (it was a four-point cubic: 35 instructions a read). Given up: with Wow on,
  a repeat loses up to 2.4 dB at 10 kHz at the worst fraction (0.6 dB at 5 kHz), on top of what Tone takes; at rest
  (Wow 0, the time reached) nothing. A tape does the same.
- **The saturators test once** for "below the knee" and only then do their arithmetic.
- **`xf_up` keeps its states in registers** (49 instructions a sample for 57); the same in `xf_down` was 67 for 65.

`tests/cr_fx_test.c` still prints the host's instruction count (COST) and now checks it as a guard against a loop
growing; it is not the unit's time. `tests/target_budget.txt`'s count for `xd_run` and `xr_run` went UP (3738 and
1974 for 522 and 1075): it weighs an instruction four times for every loop around it, and the loops are nested now
(a line, a piece of ring, a sample). It is exact from build to build and still catches a loop that grows.

**If the unit still wants less**, in this order: `CR_FX_LITE=1`; the delay at half rate (`CR_FX_DELAY_DS=2`); the
effects on the second core when Dual Core is on (a design only: the two send buses are complete when the parts are
mixed, so core 1 could run `xf_block` on a copy of them while core 0 runs the master of the previous block, at the
price of one block, 0.7 ms, of extra latency on the wet and a second pair of mix buffers; nothing of it is built).

### Late is not slow

The unit counted halves late whose render had finished well inside the budget (`pk_late_us 2687`, and `896` with the
loops in RAM). `pk_block_us` is the render alone; a half is late when it ENDS after its boundary, and that is the
render plus TIMER5 nested in it plus however long the ISR had to wait to start.

**What a late half did** (to the second build): `fm1_alnk0_irq` cleared the half's pending bit after its render.
There is one bit. The boundary that passed during a late render set it again, the ISR cleared it on its way out, and
that boundary's interrupt was gone: the next half was never rendered, and the DAC played the half it had played two
halves before. 2.9 ms of old sound spliced into the stream, a jump at both ends: inaudible in a short note, a pop in
anything that sustains, exactly as loud as the tail it cuts. The effects' state was never torn (the render simply
went on one half later); the loop's clock slipped 2.9 ms each time.

**What it does now**: the bit is cleared BEFORE the render. A boundary that passes during the render keeps its
interrupt and the ISR runs again as it returns. The late half itself is not heard: its blocks are written in order
and the DAC, which starts that half at the boundary, needs block k only 726 us x k later; the ISR must be 2.1 ms
behind before the DAC overtakes it. A late half only takes time from the next one, and the ISR catches up as long as
the average half fits. `stale_halves` counts the halves in which the DAC did play something old (by the boundary
estimate below): that is the number that must be 0, not `pk_late`. `CR_ACK_LATE=1 ./build.sh` builds the old order,
should a unit need it; `tests/audio_test.c` checks the new one.

**The boundary estimate.** Nobody tells the ISR when its boundary was. `cr_prof_entry` (`cr_cpu1.h`) expects one
every 69 659 ticks of TIMER4 from the earliest entry it has seen (an entry is never early) and moves the expectation
up by the smallest lag of every 64 halves, 43 us at most (the two clocks drift). `pk_lag_us` is the largest lag; a lag of a whole
half or more is a boundary nobody served.

**The shed** (`audio.c shed_check`: two halves in a row above 85 % fade a voice, one more each half while it lasts)
measured the whole half by the clock, the effects and TIMER5 included: it did count the fx stage. What it did not
count is the lag: a half that starts 600 us behind and renders 2400 is as late as one of 3000. It adds the lag now
(on the unit; the emulator's halves come in bursts and have no boundary).

**What masks the interrupts or runs above the audio ISR** (the worst case of each, by reading; `irq_gap_us` and
`irq_gap_pc` measure the longest on the unit):

| What | How long (read from the code and the flash's data sheet; not measured) | When |
| --- | --- | --- |
| a flash erase (`storage_hw.c st_erase`) | 45 ms by the data sheet, the buffer silenced first; **the unit read `irq_gap_us` 17 783 and 17 851 (2026-10-13), the second with `irq_gap_pc` 0x02014E9E: in that build's listing the instruction after `st_erase`'s `sti`, behind its call of `fl_erase4k_ram`: 17.8 ms is one sector erase**, five or six halves nobody served (`stale_halves` counts them: 9 erases read 46; they are silence) | EVERY save of a `storage.c` object erased its copy first until 0.22. Since then (docs/SETTINGS.md "When flash is written"): the session's and the favourites' erases are done ahead (not the settings': 3y52, their previous copy is the calibration's fallback; their save erases, as before 0.22), at power-on before the audio starts and after 10 s without a sound or an input; a save of those that still has to erase waits for 4 s of the same when the unit writes by itself (`erases_near`: 0), and SAVE held erases at once. A user sound's, a patch store's and a loop slot's save erase as before (the player's own act) |
| a flash page program (`storage_hw.c st_prog`, `fm1_flash.h fl_write`; 256 bytes a go) | ~1 ms each by the driver's comments (3 ms the generic maximum), one after the other with the interrupts open between; `flash_prog_us` is the unit's own longest (not read yet) | every save: settings (4 pages), session (5), favourites (3), a loop, a user sound |
| a flash read through the driver (`st_read`, 256 bytes a go) | ~0.5 ms each | loading a stored loop or record |
| TIMER5, above the audio ISR (the key and LED scan, USB audio's endpoint, the MIDI in): 10 kHz | not known: `pk_t5_us` is its share of the worst half (29 ticks) | always |
| the USB recording's service inside the ISR, interrupts off (`ua_isr_service`, `ua_audio`) | under 100 us a block | a computer records |
| the looper's marks (`cr_ui.c cu_lm_marks`), packing a loop (`cr_loop_pack`) | up to ~75 us (512 events) | the loop page redraws; a loop is saved |
| `cr_snapshot` | ~15 us | every UI frame |
| a sound loaded into a part (`cu_load` and its kin, `css_part_put`, `cu_pick_end`) | 5 .. 10 us | a preset changes |
| everything else under `fm1_irq_off` (46 places) | a few instructions | |

The page programs and the reads are the ones that made halves late: a millisecond without interrupts, while a CZ-1
chord's half took 2.0 .. 2.6 of its 2.9. They cannot be shortened (the code runs from the same flash: while it is
busy, nothing can run), and with the pending bit cleared first they no longer need to be. Nothing in the effects
runs with the interrupts off, clears a line, or hashes its state in the ISR beyond the 16 parameters a block.

### CZ-1

Its cost is steady, not a note-on peak: `cz_native_render` runs, for every sample and each of a tone's two lines, a
call to `cz_native_wave` (two switches, the cosine table) and a 62-instruction loop: about 270 instructions a sample
for a two-line tone, 34 000 a half, **~170 us a voice a half** and 20 .. 40 more for its three envelopes a line every
block. Eight voices (its polyphony, `NPOLY`): 1.6 .. 2.0 ms of the 2.9, as the unit read. Options, cheapest first,
none built (the engine is Melodee's and its samples are checked bit for bit):

1. `CZ_POLY` 6 (a define `eng_cz.c` already honours): 1.5 ms at the most.
2. `cz_native_wave` inlined, its two switches decided once a block instead of every sample: perhaps a quarter less.
3. A line whose amplitude envelope has ended skipped (its phase advanced in one step): one-line-audible tones halve.
4. The two lines' loops apart, block-wise, as the effects are now.

Built since (0.18, bit for bit; docs/CZ1.md "The render's cost on the unit"): 2 and 4, taken further (a loop for
each wave and window, the carrier from a table), and the larger find of the audit that followed: the envelopes'
definitions were rebuilt for every voice and block. Counted: about 1 980 us -> 630 us for eight voices. 1 is not
needed for it; 3 cannot be done sample for sample.

## Code in RAM (3y41)

The firmware executes in place from the SPI flash (XIP, through the chip's code cache). Two readings of the owner's
unit, 2026-10-13, the same playing both times (the P5 engine on the chord part, four voices, and on the bass part;
reverb, delay and chorus up), peaks in microseconds of a half's 2902:

| build | `pk_block` | `pk_part0` (chord) | `pk_part1` (bass) | `pk_fx` | `pk_master` | `pk_late` | `stale_halves` |
| --- | --- | --- | --- | --- | --- | --- | --- |
| 40acb01, all code in flash | 3136 | 2084 | 1414 | 717 | 187 | 83 | 22 |
| the same with `P5_RAM=1` | 2623 | 1533 | 1317 | 716 | 159 | 1 | 0 |

`P5_RAM=1` links the P5 engine's two per-sample functions (`p5_samples`, `p5_mod_samples`: 3792 B) at RAM instead of
flash. **The chord part got 26 % cheaper and not one instruction changed** (`tools/ram_code.py cmp`: the two
functions are the same 1149 and 155 instructions in both builds). Melodee's notes say the same of its first hardware
run, and ChoralRoot's own count for CZ-1 has an unexplained gap of the same kind (1066 us measured, 630 counted:
docs/CZ1.md). The effects' four loops in RAM changed nothing in 0.18 (`CR_FX_RAM`, above): small loops that run many
passes stay in the cache; a 3.3 KB loop body that runs once a sample apparently does not. **That is a hypothesis.**
What follows is the means to measure it, stage by stage; nothing but `P5_RAM` and (since 3y177) `CR_RAM_VA` is on by
default.

**`P5_RAM` is 1 in a device build since 3y41** (`tools/build.py`; `P5_RAM=0 ./build.sh` is the all-flash build, byte
for byte 40acb01's default). The host and the emulator never see the flag.

**`CR_RAM_VA` is 1 in a device build since 3y177** (`tools/build.py RAM_DEFAULT_ON`; `CR_RAM_VA=0 ./build.sh` is the
VA from flash). The unit, with the VA chord shared between the cores (docs/DUALCORE.md "The VA's chord on both
cores"), MORPH PAD 8 notes + PUNCH BASS: the block 2389 -> 2103 us dry and 2574 -> 2287 recording, part0 1498 ->
1289, part1 1342 -> 1159, no late half and no shed in either; FM6 TINE EP over the VA's DEEP SUB on core 1: part1 576
-> 249, the block 2917 -> 2565 recording and its 10 late halves / 31 sheds gone. So for the VA the flash WAS the cost
(two cores running `va_render` from XIP at once, or one: the bass alone gains 57 %). RAMTEXT 16 120 of 24 576 with it
(8456 B left); another CR_RAM_* switch beside it must fit there too, or be built with `CR_RAM_VA=0`.

### The switches

Each moves one stage's functions to RAMTEXT (`0x01C00000`, 24 576 B, of which the flash driver has 2888 and the
second core's loop 128). Environment variables of `./build.sh`, all off unless set to 1:

| switch | functions (bytes) | moved | RAMTEXT with it alone (P5 in) |
| --- | --- | --- | --- |
| `P5_RAM` (default 1) | `p5_samples` 3352, `p5_mod_samples` 440 | 3792 | 6808 |
| `CR_RAM_FX` | `xd_run` 1708 (the delay), `xr_run` 2000 (the reverb), `xf_down` 308, `xf_up` 244 (the half-band filters), `xf_block` 794, `xc_block` 636 (the chorus), the eight `mix_loop*` 1800 (a part's level, pan and sends into the buses) | 7490 + an 8 B veneer | 14 308 |
| `CR_RAM_MASTER` | `master_block` 3534 (Classic), `clean_block` 948 (Clean's look-ahead limiter), `clean_post` 110, `clean_post17` 90, `clean_split` 36, `usb_fixed_dac` 46 | 4764 | 11 572 |
| `CR_RAM_CORE` | `track_render_x` 1432 (the voice loop, the envelopes, the calls of the engine), `mix_part_out` 960 (drive, level, pan, the choice of mix loop), `mod_begin` 640, `mod_voice` 390, `mod_voice_add` 122, `mod_tsrc` 82, `mod_end` 70 (the matrix), `track_lfo_tick` 272, `c1_part1_job` 100 | 4068 | 10 876 |
| `CR_RAM_CZ` | `cz_native_line_mv` 7340, `cz_native_line` 6098, `cz_native_render` 1464, `cz_native_noise` 554, `cz_native_defs` 218, `cz_env_tick` 206, `cz_native_vibrato` 134, `cz_native_pd` 122, `cz_native_krate` 106, `cz_native_amp` 90, `cz_hw_rate` 58 | 16 390 | 23 200 |
| `CR_RAM_VA` (default 1 since 3y177) | `va_render` 8378 (the oscillators and the filter are inlined in it), `va_morph` 582, `va_render_mono` 24, `memset` 12, the veneer `cr_v_va_fweights` 8 | 9046 | 16 120 |
| `CR_RAM_FM6` | `fm6_render` 3518 (the operators' kernel is inlined in it), `fm6_control` 2476, `fm6_eg_step` 138, `fm6_mulhi` 74 | 6206 + 28 B of veneers | 13 044 |
| `CR_RAM_TABLES_P5` | not code: `P5_TANH_Q15` 4098, `P5_EXP2_Q14` 514, `P5_INV_DEN` 514, `P5_TPT_G` 256, the tables `p5_samples` reads every sample | 5376 B of RAM `.data` | RAM 86 820 of 98 304 |

Together: FX + MASTER 19 072; **FX + MASTER + CORE 23 140** (1436 B spare); every switch at once would need about
54 800 and does not link. `tools/build.py` refuses a build that leaves less than 512 B of RAMTEXT. So the three engines
cannot ride with the rest: each is built alone beside P5 (CZ-1 alone leaves 1376 B).

**What still runs from flash under a switch**, by reading the listing (`python3 tools/ram_code.py list
build/choralroot.dis`):

- **The audio ISR's own body, under every switch**: `fm1_alnk0_irq` is 5656 B with `mix_block`, `audio_block`, the
  engine's tick and the event dispatch inlined into it (it calls 43 functions directly, most of them a key's or a
  loop's handling). That is `pk_tick` (clearing the five buses, `events_block`, `cr_tick`, `cr_loop_tick`), the
  mid/side sums before the master, **the DAC word packing** (`audio_block`: x 128, the scope, Clean's fraction bits)
  and the profile's own reads. It was not moved: it does not fit beside FX + MASTER + CORE, and its loops are a few
  instructions each. `shed_check`, `cr_prof_whole`, `cr_tick` (432 B), `cr_loop_tick`, `crl_tick1` stay too.
- `CR_RAM_FX`: `xf_block` calls `xf_coefs` (flash, through a veneer) only in a block in which a parameter or the
  tempo moved. A part's drive (`track_dist`) is inlined in `mix_part_out`: that is CORE's.
- `CR_RAM_CORE`: `track_render_x` calls the engine (`render`, `done`, `block`: six calls through `engine_t`'s
  pointers): flash, unless that engine's switch is on. P5's per-block code (`p5_render` 1240 B, `p5_env_tick` 342,
  `p5_block` 214, `p5_post` 130) is in flash under every switch: a candidate if C shows a gain.
- `CR_RAM_FM6`: an envelope's or the pitch envelope's change of segment calls `fm6_eg_go` / `fm6_peg_go` in flash
  (veneers in RAM; a few times a note, not per sample). `fm6_block` (per part and block, 1374 B) stays in flash.
- `CR_RAM_VA`: `va_block` (per part and block, 558 B) stays in flash. `CR_RAM_CZ`: nothing.
- The constant tables, under every switch but `CR_RAM_TABLES_P5` (below).

### The mechanism

The compiler is not told that a function will run from RAM. Two obvious ways were tried and each changed the code it
moved:

- **A section attribute** (as `P5_HOT`, `RAMFN`): this compiler does not inline a function into one that lives in
  another section unless it is `always_inline`. With the attribute on `xf_block`, `osc_sine` and `clamp` turned into
  calls, back into flash, inside the loops.
- **The calls across written through a pointer in the source**: the callee's address escapes, and the compiler drops
  what it knew of its callers. `clean_block` is compiled for `n == 32` because every caller passes it; reached
  through a pointer it grew its general path back, and `xd_run`, `mod_begin`, `fm6_control` each came out different.

So (`firmware/src/cr_ram.h`, `cr_ram.c`, `tools/ram_code.py`):

1. The sources carry **a marker**, `CR_HOT_FX` .. `CR_HOT_FM6`, in front of each function's definition. It expands
   to nothing in every build.
2. `tools/build.py` compiles as always. For the switches that are on it then calls `ram_code.place` on the object:
   each marked function's section `.text.NAME` is renamed `.rhot.NAME` (six bytes of the section-name table; a
   marked function that the compiler did not emit on its own, because it was inlined, stops the build), and
   `firmware/app.ld` puts `.rhot.*` in the `.ram_text` output section, after the flash driver. `main.c` copies the
   whole section from the image at power-on (`_rt_load` .. , up to `_rt_end`, whatever its size) and
   `fm1_guard.h`'s second PC window is `_rt_start .. _rt_end - 1`: both follow the section by themselves.
3. **Calls.** XIP (`0x02001000`) and RAMTEXT (`0x01C00000`) are 4 MiB + 4 KiB apart; pi32v2's direct call is
   PC-relative and reaches 4 MiB. The linker refuses a longer one (`relocation truncated to fit:
   R_PI32V2_LONG_JUMP_23M2`): it cannot be missed silently. A call across goes through **a veneer** on the caller's
   side, a C function the same switch compiles in (`cr_ram.c`): it loads the target's absolute address into a
   register and calls or jumps there. `ram_code.place` points the direct calls at it by rewriting the symbol of their
   relocation entries, and the veneer's own reference at the real function. The listing, both directions:

   ```
   fm1_alnk0_irq (flash)     200a666: 81 ea 92 37        call 159524 <cr_v_xf_block : 203158e >   PC-relative, to the veneer
   cr_v_clean_block (flash)  20315aa: c2 ff 04 4e c0 01  r2 = 29380100 <clean_block : 1c04e04 >   the absolute address
                             20315b0: d2 00              goto r2
   xf_block (RAM)            1c03d52: 80 ea be 0a        call 5500 <cr_v_xf_coefs : 1c052d2 >     PC-relative, inside RAMTEXT
   cr_v_xf_coefs (RAM)       1c052d2: c0 ff 20 89 00 02  r0 = 33589536 <xf_coefs : 2008920 >      absolute, back into flash
                             1c052d8: d0 00              goto r0
   xf_block (RAM)            1c03e9e: bf ea eb f6        call -4650 <xd_run : 1c02c78 >           RAM to RAM: direct, as before
   ```
   (package C's listing)

   A veneer with stack arguments is a real call (`cr_v_mix_part_out`: push, copy the two stack words, `call r4`,
   pop: 24 B). The existing RAM code uses the older form: a `volatile` pointer on the stack, then `call rN`
   (`FL_FAR`, `P5_RAM`'s `run`, `XF_CALL`). The engines' `render` and the mix loops were already reached through
   pointers and need nothing.

A build without a switch has no `.rhot` section, no veneer and an object nobody touched.

### The proof that only the place changed

`python3 tools/ram_code.py cmp BASE.dis NEW.dis` reads two listings (and `choralroot.sym` beside each) and compares
**every function of both**, instruction by instruction, with addresses replaced by what they point into (a place in
the function itself, another function, a constant by the nearest named object before it). It accepts: a function
whose address moved; a call whose target became `cr_v_NAME` for `NAME`; the veneers, which it prints whole and whose
one address must be their target's, on the other side. Anything else fails. Results, each against the default build
(`P5_SHIPPED=all`, 841 functions):

| build | moved | functions that differ | veneers (bytes) |
| --- | --- | --- | --- |
| `P5_RAM=0` -> the default | 2 | 0 | 0 |
| `CR_RAM_FX` | 14 | 0 | 3 (40) |
| `CR_RAM_MASTER` | 6 | 0 | 5 (40) |
| `CR_RAM_CORE` | 9 | 0 | 5 (72) |
| `CR_RAM_CZ` | 11 | 0 | 0 |
| `CR_RAM_VA` | 4 | 0 | 1 (8) |
| `CR_RAM_FM6` | 4 | 0 | 3 (40) |
| FX + MASTER (package B) | 20 | 0 | 8 (80) |
| FX + MASTER + CORE (C) | 29 | 0 | 13 (152) |
| C + `CR_RAM_TABLES_P5` (D) | 29 | 0 | 13 (152) |

No function's instruction count differs. (The tool is not blind: `CR_FX_LITE=1` against the default fails on
`xr_run`, 675 -> 677 instructions.) The default build's listing is byte for byte the one `P5_RAM=1 ./build.sh` made
at 40acb01, and `P5_RAM=0`'s is 40acb01's default. `tests/target_budget.py` reads functions by name and passes on
every one of these listings unchanged. The host and the emulator build none of it: the 246 golden renders and the
17 suites are the proof on that side.

### The tables

A table in flash is read through the same cache as the code. What the hot paths read, by the listing
(`.rodata` is XIP; FM6's five are RAM `.data` already, 11 780 B, since Felucca):

| path | every sample | every block or voice |
| --- | --- | --- |
| P5 | **`P5_TANH_Q15` 4098** (seven look-ups a sample), `P5_EXP2_Q14` 514, `P5_INV_DEN` 514, `P5_TPT_G` 256: 5382 B, XIP; RAM with `CR_RAM_TABLES_P5` | `PITCH_OCT` 768, `P5_ENV_ATK` 512, `P5_ENV_SSM` 512, `P5_ENV_EXP` 256, `P5_LFO_INC` 512, `P5_AMOUNT_Q15` 256 |
| CZ-1 | `CZ_CARRIER` 4096 (every loop), `CZ_RECIP` 8192 (the `_mv` loops and NOISE): XIP | `CZ_AMP` 256, `PITCH_OCT` |
| FM6 | `FM6_SIN` 4100, `FM6_MKI_LOG` 4096, `FM6_MKI_EXP` 2048, `FM6_OPL_LOG` 1024, `FM6_OPL_EXP` 512: **RAM already** (since 3y127 `FM6_SIN` is built at power-on and the two LOG tables mirrored from stored halves: `.bss`, not `.data`) | `FM6_FREQ` 4100, `FM6_EXP2` 4100, `FM6_ALG` 192, the small ones: XIP |
| VA | `SINE` 2050, `TANH_Q15` 514: XIP | `SVF_G` 256, `PITCH_OCT` 768, `ENV_EXP` 256, `ENV_LIN` 512 (one copy with `P5_ENV_ATK`: the same bytes), `VA_BROWN_*` |
| FM TONE | `QUAD_HARM` 27 702, `SINE`, `TANH_Q15`, five of 256: XIP | |
| the chorus, the effects' block | | `SINE` (the LFOs, once a block), `LFO_INC` |
| the delay's and the reverb's loops | none | |
| a part's drive, the master | `TANH_Q15` 514 (when the drive is on; the limiters' soft clip) | `LEVEL_Q12` 256 |
| the matrix | | `TP` 1820, `PITCH_OCT` |

RAM has 16 860 B free (81 444 of 98 304), 11 484 with the P5 tables in. POOL (316 052 of 344 064) is zero-filled
memory with no image behind it: a table there would need a copy loop at power-on, and the build keeps 8 KB of it
spare. **Worth a RAM copy, as an estimate only**: the P5 four (built: `CR_RAM_TABLES_P5`); then `SINE` + `TANH_Q15`
(2564 B: VA, the chorus, the drive, both limiters, FM TONE, WHEEL); then `CZ_CARRIER` (4096 B). `CZ_RECIP` (8192)
fits only without the P5 tables; `QUAD_HARM` does not fit. None but the first is built: D against C says first
whether a table in RAM is worth anything.

### The measurement plan

Eight packages in `build/scratch/p5-ram/` of the main checkout (its `README.txt` has each one's switches, hash and
size line), all with the 200 programs:

| package | switches |
| --- | --- |
| `choralroot-0-all-flash.fwsc` | `P5_RAM=0`: the first row of the table above, for a control |
| `choralroot-A-p5.fwsc` | the default: the second row (byte for byte the build that was read) |
| `choralroot-B-p5-fx-master.fwsc` | A + `CR_RAM_FX` + `CR_RAM_MASTER` |
| `choralroot-C-all-audio.fwsc` | B + `CR_RAM_CORE` (the engines left out: they do not fit) |
| `choralroot-D-all-tables.fwsc` | C + `CR_RAM_TABLES_P5` |
| `choralroot-E-p5-cz.fwsc`, `-F-p5-va.fwsc`, `-G-p5-fm6.fwsc` | A + `CR_RAM_CZ`, `CR_RAM_VA`, `CR_RAM_FM6`, one each |

For each: install, **power-cycle**, read `python3 tools/fm1_install.py --debug` once to clear the peaks, play, read
again. The peaks are the worst half since the last read, so play the same thing for the same time (30 s is enough).

- **A, B, C, D**: the playing of the table above: P5 on the chord part (01 ITS PROPHET5, four keys held, then
  changed a few times) and P5 on the bass part, reverb, delay and chorus sends up. Compare `pk_part0_us`,
  `pk_part1_us`, `pk_fx_us`, `pk_master_us`, `pk_tick_us`, `pk_block_us`, and `fx_delay_avg_us`,
  `fx_reverb_avg_us`. Expect: A = the second row. B against A: `pk_fx_us` and `pk_master_us` (0.18 said the four
  loops gain nothing; the chorus, the mix loops and the limiter were not in that test). C against B: `pk_part0_us`
  and `pk_part1_us` (the framework around the engine). D against C: `pk_part0_us`, `pk_part1_us` (the tables).
- **E**: CZ-1 on the chord part, eight keys (`python3 tools/cz_pop_check.py --play` plays it): `pk_part0_us` against
  A's with the same playing, and against the count's 630.
- **F**: a VA chord and bass (LUSH PAD). **G**: an FM6 chord. `pk_part0_us` against A's with the same playing.
- With every package: `pk_late`, `stale_halves` (the rule: 0), `flash_erases`, `flash_holes` (the rule: 0), `erases_near` (the rule: 0).

Nothing here has run on a unit but A (as 40acb01 + `P5_RAM=1`).

### What to keep in mind

- **RAM code can be overwritten; flash code cannot.** The write guards (`fm1_guard.h`) cover the two stack bands and
  the top of RAM, not RAMTEXT: a stray write into `0x01C00000 .. 0x01C05FFF` would change audio code silently where
  the same write is harmless today. It is so for the flash driver and for P5's loop already; B and C put 12 to 16 KB
  more there.
- **The PC guard** allows two windows, XIP up to `_etext` and `_rt_start .. _rt_end - 1`; the second covers
  `.ram_text` with `.rhot.*` in it and `.c1_text`, whatever their size, on both cores. Nothing else in the firmware
  looks at a PC's range (`irq_gap_pc` is reported, not judged: a value in `0x01C0....` now names a RAM function, in
  the same listing).
- **The second core** runs `c1_part1_job` and `track_render_x` from RAM under `CR_RAM_CORE`, `.c1_text` sits behind
  `.rhot.*` instead of right behind the flash driver; `fm1_c1_main`'s address is the linker's either way. A Dual
  Core session belongs in C's reading.
- **The flash driver's rule is unchanged**: below `_rt_hot` nothing may call (`tools/build.py` checks the listing);
  the audio code above it runs only with the flash on (the driver masks the interrupts and core 1 works only inside
  core 0's audio interrupt).
- A veneer is one more call level: 8 bytes of stack for the three with stack arguments, twelve to thirteen far
  calls a block in C (about forty instructions a half).

## The delay
## The delay

```
                       +-------------------- feedback (0 .. 0.988, eased) ------------------+
                       v                                                                    |
 in --(+)--> sat --> [ line L, 32768 x int16 ] --> read at tL + wow --> low-pass (Tone) --> low cut 40 Hz --+--> wet L
 in --(+)--> sat --> [ line R, 32768 x int16 ] --> read at tR + wow --> low-pass (Tone) --> low cut 40 Hz --+--> wet R
                       ^                                                                    |
                       +--------------------------------------------------------------------+
   Stereo: each side feeds itself, both take the input.
   Ping-pong: L takes the input and R's return, R takes L's return only (the crossfade between the two: 6 ms).
```

- **Two lines of 32768 samples** at 44.1 kHz (0.743 s), `int16`, in `.pool`: 131 072 B. They hold half the send
  (`XF_IN`): a loud chord's send is about +-40000, so 20000 in the line, as Felucca's delay took it.
- **The read** is at a Q8 position, between two samples (linear; a four-point cubic to the second build: "The cost on
  the unit"), that **glides** to its target: a lag of ~46 ms, at most half a sample a sample, a block's part of the
  way as a ramp. A new time (the knob, the tempo, a MIDI clock, Sync or Spread) bends the pitch of what
  is in the line by at most a fifth up or an octave down for a moment, as a tape or a BBD clock does, and never
  clicks. At rest the position is a whole sample and the read is that sample itself.
- **The loop**: read -> one-pole low-pass (Tone, 400 Hz .. 22 kHz, an octave every 22 steps; the default, 70, is
  3.8 kHz) -> one-pole low cut at 40 Hz -> feedback gain -> + the input -> the saturator -> the line. Every repeat is
  darker than the last. The wet is taken after the filters, so the first repeat is already coloured.
- **The saturator** (`xd_sat`) is a straight line up to half of the line's range and a parabola above it whose slope
  falls to 0 at the line's limit: no edge. A repeat under the knee has its harmonics below -90 dB (measured -92 dB at
  -12 dB of the line); at full scale it is -27 dB, a soft clip.
- **Feedback** tops out at 0.988: at the longest time that is seven minutes to -60 dB, at 16 ms eight seconds. It
  does not self-oscillate (a decision: see the end).
- **Time**: Sync on: a division of the tempo, `1/32 1/16T 1/32. 1/16 1/8T 1/16. 1/8 1/4T 1/8. 1/4 1/2T 1/4. 1/2`
  (`XF_DIV`, 48ths of a quarter note; the tempo is `xf_bpm`, the engine's 20 .. 300 BPM, MIDI clock included). A
  division longer than the line plays at half. Sync off: 16 .. 740 ms, exponential over the knob. Both values are
  kept; Sync chooses. **KNOB 1 clockwise is faster** (the owner 2026-10-11): a smaller division, fewer milliseconds;
  `cr_ui.c`'s knob handler turns the detents round for this cell alone. The stored values (`XP_DDIV`'s index,
  `XP_DTIME`'s 0 .. 127, larger = longer; the session's tags 10 and 25) and the read-out are as they were, and the
  echoes glyph still spaces its bars by the time, so they close up clockwise.
- **Spread** is the right side's time as a part of the left's. Sync on: `1:1`, `3:4`, `2:3`, `1:2`. Sync off: `x1.00`
  .. `x0.50`. The right side is always the shorter one, so Time is the longest time and always fits the line.
- **Wow**: the time's wobble, a 0.33 Hz sine and an eighth as much of a 3.1 Hz one, 0 .. 2 ms. The SAME on both
  sides: with Spread 0 in Stereo the two sides stay the same sample (the FM-1's speaker is mono).
- **Level**: `XD_WET` (Q12) is the Stereo return, `XD_WPP` the 3 dB more that ping-pong gets (its repeat is on one
  side only). Matched to Felucca's delay: see "The level".

## The reverb

```
 in --> low cut (2 poles, 20 .. 500 Hz) --> pre-delay (0 .. 138 ms, glides) --> 4 all-pass diffusers --> x input gain --+
                                                                                                                        |
     +------------------------------------------------------------------------------------------------------------------+
     v
   (+)--> [ 8 lines, 57 .. 125 ms at Size 127, one ring of 16384 x int16 ]
     ^         | read at a gliding length (Size); lines 1 3 5 7 also swing on two slow sines (Mod)
     |         v
     |      one-pole low-pass (Damp) --> x g_i (Decay: the same RT60 for every line)
     |         |
     +--- Hadamard 8 x 8 x 181/512 <---+          wet L = l0 - l1 + l4 - l5, wet R = l2 - l3 + l6 - l7, then Width
```

- **The topology** is a feedback delay network: eight delay lines whose outputs are mixed by an orthogonal matrix and
  fed back (Stautner and Puckette, "Designing multi-channel reverberators", Computer Music Journal 1982; Jot and
  Chaigne, "Digital delay networks for designing artificial reverberators", AES 90th Convention 1991), behind
  Schroeder's all-pass diffusers (Schroeder, "Natural sounding artificial reverberation", JAES 1962). The matrix is
  the 8 x 8 Hadamard matrix: three stages of add / subtract, no multiply, then one scaling by 181 / 512. The code is
  written here from those descriptions; nothing is taken from another implementation.
- **The rate**: 22.05 kHz, between a two-path polyphase all-pass half-band filter down and a pair of them up
  (`xf_down`, `xf_up`; `tools/fx_halfband.py`: 2 + 2 first-order sections, Q11 `199, 1264 | 695, 1785`, -54 dB above
  12.55 kHz, flat to 9.5 kHz; they run on the send and the wet at full resolution and keep their remainders).
- **The lines** at Size 127, in half-rate samples: `1259 1433 1607 1801 2003 2221 2477 2767` (primes; 57 .. 125 ms),
  0.706 s in all, in ONE ring of 16384 `int16`, 32 768 B. Size 0 .. 127 scales all of them from a quarter to the
  whole; the lengths **glide** (a size change bends the tail's pitch by up to 17 % for a quarter of a second) and
  settle on whole samples.
- **Modulation**: lines 1 and 5 swing on a 0.31 Hz sine and its quarter turn, 3 and 7 on a 0.47 Hz one, 0 .. 0.9 ms
  (Mod). Each side's output has two still and two swinging lines. A swinging line is read between two samples
  (linear), which costs it treble at every pass: with Mod up the top of a long tail fades first.
- **Decay** is the RT60: every line's gain is `10^(-3 L / RT60)` for its own length L (`xf_coefs`: an integer
  `e^-x`, no table). 0.3 .. 72 s; the top position is **"Inf"**: a gain of 1 - 2^-13 a pass, **and the damping out
  of the loop** (a one-pole takes a little of everything at every pass: at Damp's default, 3.4 kHz, 1 kHz is gone in
  15 s whatever the gain). With Mod at 0, "Inf" loses 1 dB in 30 s and 21 dB in ten minutes.
- **Damp** is a one-pole low-pass in every line (open at 0, 12 kHz .. 660 Hz). **Low cut** (two poles, 20 .. 500 Hz) is
  in front of everything. **Pre-delay** 0 .. 138 ms (it glides too), and the **diffusers**: four all-passes of 113,
  173, 263 and 397 samples, g = 5/8, in a second ring of 4096 `int16` (8 192 B) with the pre-delay.
- **The input gain** follows the decay as `(1 - g^2)^(1/4)`, so that Decay 72 s is ~6 dB louder than 3 s, not 14.
- **Width** scales the side signal: 0 is a mono reverb, 127 the two sides as they are.

### Why it cannot blow up

The Hadamard matrix times `1 / sqrt(8)` is orthogonal; 181 / 512 = 0.353516 is a hair under it. Every line's gain is
at most 32764 / 32768; a one-pole low-pass and a linear interpolation never exceed 1 at any frequency; the knee and the
clamp at the line only take. One pass round the loop multiplies the energy by less than 1, whatever the parameters;
the rounding adds at most a unit's worth of noise, which the loop's own loss takes away again.

## The arithmetic, and where a tail ends

- **In a loop, a product keeps its remainder** (`xf_rs`): the delay's two filters and its feedback, the reverb's
  low-pass, gain and line write (three a line), the low cut, the half-band's sections. Nothing is lost over time and
  nothing is biased; with a silent input the result is exactly 0.
- **Feed-forward products round towards zero** (the diffusers, the interpolations, the input gain).
- **A tail ends at one unit of the line.** When a whole block has written nothing larger than +-1 (-90 dBFS of the
  16-bit line; at the output under -77 dBFS at full MASTER even with every line of the reverb at once, in practice
  -84), the loop's products are rounded towards zero for the next block: the last single units are not sent round
  again and the lines reach exactly 0 one pass later. A block that writes anything larger is back to the remainders
  at once. **Nothing above one unit is ever cut**, and there is no other threshold: an effect's loop is skipped only
  when every sample of its lines and every state of its filters is 0 (`xf.d_live`, `xf.r_live`). With both off a
  block costs 2 us.

Measured (`cr_fx_test`):

| | |
| --- | --- |
| a 50 ms burst into the delay, feedback 127, at 16 ms | 2.6 dB a quarter second in every window to -80 dBFS; 8 s of tail (a quiet burst, 30 dB lower: the same slope, 5.8 s) |
| .. at 60 ms, a quiet burst | 0.7 dB a quarter second to -84 dBFS, 16.5 s |
| .. at the longest time, feedback 120 | 1.8 dB every two seconds to -82 dBFS, 54 s |
| the delay's tail from -20 to -80 dBFS: its noise (the wet of a send and of the same send upside down, halved sum) | -91 dBFS at -20, -90 at -50, -89 at -80: never less than 9 dB under the tail |
| the reverb's tail (Decay 7 s) from -20 to -80 dBFS: its noise | -85 / -86 / -86 dBFS: never less than 6 dB under the tail |
| the reverb at "Inf", Mod 0, Damp 0, ten minutes | the tail -24 -> -45 dBFS; the rounding's noise, which goes round with it, 40 to 28 dB under it (-68 dBFS at its loudest) |
| RT60 of a click for the knob's 0.3 / 1.2 / 2.4 / 4.8 / 9.6 s | 0.34 / 1.19 / 2.37 / 4.64 / 9.06 s, at any Size |
| 48 tails (Decay 0.3 .. 19 s x Size x Damp x Mod) | all end in exact silence with the reverb off, the longest after 2.3 x its RT60 |
| every corner, 5 s of noise and 5 s of a square at +36 dB (four parts' sends at their clamps), 60 s | bounded; exactly 0 at the end for every finite decay |
| a 220 Hz send at +12 dB into both | the wet peaks at 3.5 x full scale (the master's limiter and tanh follow), its largest sample step 5.7 % of the peak: no wrap, no edge |
| 16 parameters swept under a held tone and its tail at 30, 6 and 1 ms a detent; the tempo stepped | no step: the wet's second difference stays under a quarter of its largest slope |

Where each stage saturates: a send is clamped at 4 x full scale (it comes out of the parts' own clamps); the delay's
line at `xd_sat` (soft); the reverb's input at the line's range (a clamp, 6 dB above the loudest chord), its lines
at `xr_sat` (a knee at 3/4, then the limit); the half-rate wet at 4 x full scale before the up-sampler (a clamp that
the lines' own limits keep it from reaching); then the master's limiter and tanh, as for the dry sound.

The lines are 16 bits. 32 bits for the reverb's lines would put its noise 48 dB lower and cost 33 KB, which is the
delay's last 0.15 s; with the numbers above it was not taken.

## Memory

| | Bytes |
| --- | --- |
| the delay's two lines, 2 x 32768 x 2 | 131 072 |
| the reverb's eight lines, 16384 x 2 | 32 768 |
| its pre-delay and diffusers, 4096 x 2 | 8 192 |
| state (`xf`) | ~1 000 |
| **total, all in `.pool`** | **173 032** |
| Felucca's delay line and ROOM / SPRING buffers, gone from the image | -142 940 |

Size line: XIP 444 148 B (0.17's: 436 928), RAM 85 156 B (84 944), **POOL 326 316 B of 344 064** (296 224):
17 748 B free.

## CPU

A half is 128 frames, 2902 us. The unit's time is "The cost on the unit": about 290 us for both effects. The
emulator's figures (259 host instructions a microsecond) are 1.2 to 1.7 times low for these loops and 2.2 times low
for CZ-1; they are good for comparing one build with the next on the same scenario, no more.

`sh tools/emu/perf.sh`, one core, "fx" the whole stage (the parts' mix into the buses, the chorus, the delay, the
reverb), the emulator's us:

| Scenario | fx stage avg / max, second build | third |
| --- | --- | --- |
| (a) TINE EP chord | 320 / 557 | 267 / 463 |
| (b) FM PAD chord | 313 / 640 | 261 / 449 |
| (c) FM PAD + bass + loop + arp | 324 / 574 | 274 / 481 |
| (f) VA chord + bass | 265 / 558 | 227 / 462 |
| (g) CZ-1 chord + bass | 266 / 579 | 226 / 485 |
| (l) (c) with both sends at 127, Decay 13 s, Feedback up | 284 / 701 | 244 / 454 |

```
fx parts (device us per half, avg / max): down 12 / 71, delay 58 / 145, reverb 126 / 172, up 20 / 79
```

(second build: `down 12 / 123, delay 121 / 209, reverb 116 / 237, up 22 / 67`. The reverb's figure went UP on this
host, which no longer runs it four lines at a time, while its instructions on the unit halved: the emulator's number
for a stage was never the unit's.) The second core is not used for the effects: they run on core 0 after both parts
are mixed, exactly where Felucca's ran, and the split render's "one core's samples bit for bit"
(`tests/cr_cpu1_test.c`, `perf.sh`'s Dual Core runs) holds as before.

## The level

The new effects' returns are set once so that the same send gives the same loudness as Felucca's (`XD_WET`, `XD_WPP`,
`XR_WET` in `cr_fx.c`: the one place):

| Measured | Old | New |
| --- | --- | --- |
| the reverb under a held chord (1 .. 3 s, RMS a side), both at their defaults, the same send | 4122 | 4303 (+0.4 dB) |
| the delay's first repeat (both sides' energy), Stereo | 0 dB | +0.0 dB |
| the delay's first repeat, ping-pong (one side, 3 dB up) | 0 dB | +0.1 dB |

The defaults: Reverb Size 88, Damp 56, Decay 3.0 s, Pre-delay 17 ms, Mod 56, Low cut 127 Hz, Width 127; Delay Sync
on, 1/8, Spread 1:1, Feedback 58, ping-pong, Tone 70 (3.8 kHz), Wow 24 (free time 250 ms, free spread x1.00).

## The chorus, stereo (0.19)

Felucca's chorus (`fx.c fx_buses`) is a line of 2048 samples (`cho_buf`, 4 KB of the pool) that the chorus send is
written into at half level, read at one point that a sine LFO moves between 400 and 400 + 6 x Depth samples back (9.1
.. 17.2 ms at the default Depth 60; Rate is the LFO's), and returned to **both sides alike**: beside the dry sound a
comb, the same in both ears. It ran on the unit to 0.18 (with `CR_FX` 1 it was all that was left of `fx_buses`).

**0.19, `cr_fx.c xc_block`** (the owner, 2026-10-12: "chorus improvements sound good"): the same line, written as
before, read at **two points**: the left ear's is Felucca's own, the right ear's the same LFO **a quarter turn ahead**.
Both come back at the level one did, straight into the mix.

| | 0.18 | 0.19 |
| --- | --- | --- |
| each ear beside the dry sound, the default send (45: the wet 9.0 dB under) | notch -3.8 dB, peak +2.6 dB | the same |
| the two ears' wet against each other | the same signal | unrelated: correlation -0.01 over 100 Hz .. 10 kHz (0.005 on noise through the code) |
| the mono sum's deepest notch | -3.8 dB | -3.8 dB, at the two moments of a turn where the taps cross, never deeper |
| the mono sum, the share of band and time more than 3 dB down | 16 % | 5 % |
| what is left of the chorus's motion in the mono sum | all | 71 % (half a turn apart, as SLOOP has it: none, a comb that stands still) |
| the mono sum's level | | 0.0 .. 0.4 dB lower at the default send, 0.8 .. 1.5 dB at send 105 |
| Rate, Depth, the send, the line, the pool | | as they were |

(`tools/chorus_response.py`, computed from the firmware's numbers; `tests/cr_fx_test.c` on the code;
[AUDIO-QUALITY.md "The chorus, stereo"](AUDIO-QUALITY.md#the-chorus-stereo) has the tables with the full send and
the comparison with half a turn.)

- **How**: the LFO is taken once a block, at its start and at its end, for each tap (four sines a block where there
  was one a sample), and each tap's read point is a straight line between the two, kept at a 32nd of the line's own
  resolution so that it ends exactly on the next block's first point: no step in the read position **while Depth and
  Width stand still** (0.19, 0.20; the next bullet). The left tap against 0.18's own, on noise: 45 dB under it (the
  line's resolution of a 256th of a sample).
- **While a knob turns (0.21)** (the owner: "This FX pops and crackles when adjusting the depth which I don't
  like."). The cause: each block's line ran from the LFO's point at the block's start to its point at the end *with
  that block's Depth and Width*, so the next block began where this one ended only while neither moved. A step of
  Depth put the next block's first point up to 6 samples away for each unit of Depth (the FX layer's detent is 2
  units: 12), a step of Width the right tap's up to 4.7: a click a detent. The fix, two parts:
  - **the read point is kept**: each tap's last point (`xc.ra`, `xc.rb`, Q8: where the last block's line aimed) is
    state, and the next block's line starts there, whatever the parameters did. Continuous by construction; and a
    running block takes two sines where it took four;
  - **Depth and Width glide**: the depth the block uses (`xc.dc`, in 16ths of the old unit) and the right tap's lead
    (`xc.oc`, the LFO's phase) go a 64th of what is left a block, rounded away from zero: a one-pole of 64 blocks,
    **46 ms** (`XC_GL` 6), that lands exactly on what is set (the last 64 units one a block). Without it the kept
    read point would make a 6-sample step up inside one block of 32: a pitch blip of 19 % instead of a click.
  At rest the glided depth is 16 x the old one and `(s x 16 d) >> 12` is `(s x d) >> 8`: **the block is 0.20's to
  the bit** (`tests/cr_fx_test.c` "chorus glide" keeps 0.20's block beside the new one: noise and a sine, six
  settings, blocks of 32 and of 8 / 24 / 16; the golden renders: 0 changed). An empty line (`xc_live` 0) glides
  nothing: the first block with a send takes what is set and its own first point, as 0.20's did, so a note after a
  knob turned in silence, after power-on or after a session is loaded starts in place. Rate needed nothing: it is
  the LFO's increment and the phase runs on (the test: Rate stepped, swept and thrown gives 0.20's samples).

  | the read point's largest move in one sample (samples); a 110 Hz sine's largest second difference | 0.20's block | 0.21 |
  | --- | --- | --- |
  | at rest, Depth 60 / Depth 127 | 0.06 / 0.13; 12 | the same |
  | Depth 60 -> 61 | 6.0; 1862 | 0.06; 12 |
  | Depth 0 .. 127 .. 0, a unit every 2 or 3 blocks | 6.0; 1878 | 0.13; 12 |
  | Depth 0 -> 127 at once (no knob does it) | 561; 9738 | 0.31; 76 |
  | Width 60 -> 61 (Depth 127) | 3.3; 212 | 0.13; 12 |
  | Width 0 .. 127 .. 0 | 4.8; 1472 | 0.19; 12 |
  | Width 0 -> 127 at once | 516; 15106 | 0.38; 82 |

  (the read point is measured from the output: the send is a ramp, so a tap's output is the time it reads at; the
  grain is a 16th of a sample.) **Why 46 ms**: with 23, 46 and 93 ms a detent and a sweep by hand measure the
  same, the steady chorus's own figures; they differ only when the whole range is thrown at once (0.56 / 0.31 /
  0.19 samples a sample, second difference 152 / 76 / 38), which a knob cannot do, and 93 ms would lag the hand
  twice as long and take 1.6 s to land the Width. One unit of Depth at 46 ms bends the chorus's pitch by at most
  5 cents, fading over that time (the FX layer's detent, 2 units: 10 cents; computed from the glide's first block,
  6 / 64 samples in 32): under what the chorus itself swings (18 cents each way at the default Rate and Depth).
- **Nothing sent, nothing run**: `xc_live` counts the samples for which the line still holds something. While it is 0
  a block costs a look at the send (32 loads); with a send, or until the line has run out (2048 samples of silence
  written), the loop runs. Zeros in, zeros out, to the bit.
- **In both Output modes**: the chorus is an effect, not a part of the output stage. A sound with a chorus send is
  therefore no longer 0.18's in Classic either; without one it is, bit for bit
  ([AUDIO-QUALITY.md "What Classic's guarantee is now"](AUDIO-QUALITY.md#what-classics-guarantee-is-now)).
- **Credit**: two taps on the one line, left and right, and the LFO a block with the read points a line across it are
  the design of SLOOP 2.6's chorus (its `firmware/src/fx.c fx_buses`; GPL-3.0-only): the design, written anew, no
  code copied (`cr_fx.c`'s header, `LICENSING.md`). SLOOP's taps are half a turn apart; the quarter turn, the width
  parameter, the exact line and the idle path are ChoralRoot's.
- **The test switch**: `CR_CHORUS_MONO` (`fx.c`): 0 the unit (stereo only; Felucca's one-tap loop is not in the image);
  1 a build with 0.18's return; 2 the emulator's: stereo, and 0.18's return with `EMU_CHORUS=mono`
  (`tools/emu/classic_exact.sh`, `tools/chorus_listen.sh`). With it the sums are 0.18's, bit for bit.

**The cost on the unit** (the listing, 205 instructions a microsecond): 0.18's loop was 48 instructions a sample
(the sine among them): 30 us a half, whether a part sent or not. `xc_block`: 52 a sample and about 110 a block (the
four LFO points): 35 us a half while it runs, 3 us while nothing is sent; and the master's mix line no longer adds a
wet buffer (3 instructions a frame, 2 us). **+3 us a half with a chorus send, -29 us without.** Flash: `xc_block` is
546 bytes (the whole round: +564 B); RAM: the mono wet's 128 bytes gone; the pool unchanged.
**0.21** (the listing again): 51 a sample and about 90 a block at rest (two sines, not four; the two glides are a
compare each), about 106 while a knob's glide runs, 10 more in the one block that starts after rest; an idle block
161 instructions where it was 156. A running block: 1722 instructions for 1778, **-0.3 us a block, -1 us a half**.
Flash: `xc_block` 632 bytes (+82), the whole change +144 B (`.text` 468208); RAM +16 B (the four words of `xc`);
the pool unchanged (315496 B, 91.7 %).

**Width (0.21: KNOB 3 of the Chorus page)** (the owner: "Since it's stereo now, and we have Knob 3 as empty slot we
should be able to add a parameter for Stereo Width."). `xfp[XP_CWIDTH]` (`cr_fx.c`, 0 .. 127, default 127): the
right tap's lead as a part of a quarter turn; 127 is the chorus of 0.19 and 0.20, 0 is both ears the left tap, 0.18's
mono return (the same sample left and right). The cell is a generic `xfp` cell of the layer (`cr_ui.c CU_FXP`'s
Chorus row; 2 a detent, as every 0 .. 127 cell there), reads a number 0 .. 127 as the reverb's Width does and has its
glyph (`CR_G_WIDTH`). It glides as Depth does (above). **Kept in the session** under a new tag, `CSS_FXP_XC_WIDTH`
(30, appended; `cr_settings.c CSS_FXG`): a session saved by 0.19 or 0.20 has no such tag and loads with Width 127;
0.19 and 0.20 skip the tag when they read a session that has it (nothing else in the record is touched; their chorus
is Width 127), and their next save writes the record without it, so the Width is 127 again on the way back
([SETTINGS.md](SETTINGS.md)). Listening files, from the host test's renderer (the firmware's block, not a unit):
`../audio-0.21/chorus/` (`tools/chorus_glide_listen.sh`); 0.19's are in `../audio-0.19/chorus/`. Not played on the
unit yet.

## The master, and Options > Output (0.19)

What comes after the effects: the mix and the chorus's wet through the output chain to the DAC word. Two chains
since 0.19, chosen in Options > Output, both in `fx.c` (`out_master`); [AUDIO-QUALITY.md](AUDIO-QUALITY.md) has
the measurements, the reasons and the harness.

| | Classic (0.18's, bit for bit) | Clean (0.19) |
| --- | --- | --- |
| the mix line | `((mix + wet) >> 2) * MASTER >> 10` (Q15) | `(mix + wet) * 0.75`, kept in Q17 (`CLN_TRIM`) |
| DC blocker | 2 Hz | the same (the same state) |
| LOWCUT / BASS+ | Felucca's, if `fx_lowcut` (held at 0 by ChoralRoot since 0.19) | none |
| delay | none | 32 samples, 0.73 ms (`cln_dl`, 256 bytes of RAM): the limiter's look-ahead |
| limiter | `LIM_T` 18000; release 93 ms; the gain eased over 1.5 ms after the peak (`fx_smooth`); `(x >> 4) * g >> 11` | `CLN_T` 26000; the peak held 200 ms and again by every peak within 1.2 dB, a hit's (more than 3.5 dB above the envelope) 40 ms (AUDIO-QUALITY.md "Round 3"; it was 40 ms and 0.3 dB), release 186 ms; the gain a straight line across each block of 32 to what the loudest sample of the block coming out of the delay and of the one after it needs: down before the peak, no sample over the threshold; the exact product |
| clip | `softclip`: a tanh over the whole range | `clean_knee`: linear to 26000 (the threshold), a tanh above: a safety net the limiter keeps everything under |
| MASTER | in the mix line (USB Level = Fixed: after, `usb_fixed_dac`) | last, into the 24-bit word (`clean_post17`; Fixed: `clean_split`, then `clean_post`) |
| the DAC word | the Q15 sample `<< 7` | Q22: the Q15 sample and the 7 bits under it (`out_frac`, `audio.c`) |
| a frame on the unit | 76 instructions idle, about 92 limiting | 76 idle, about 104 limiting (before the look-ahead: 83, about 99) |

Clean's look-ahead (0.19; one block ahead with a linear glide between blocks is Sloop 2.6's design, `LICENSING.md`):
the output is 0.73 ms late. Everything that goes through the master is late together. The metronome's click
(`cr_click_mix`, mixed after the master) and the USB recording's chord and bass pairs (taken before it) do not go
through it; they led Clean's output by 0.73 ms until the click was made 32 samples later in Clean (`cr_ck_wait`: it
is generated, so it needs no line) and the part pairs got a line of their own (`fx.c ua_parts_late`, 256 bytes of
RAM, 9 us a half while the computer records through Clean): the three stems of a recording line up to the sample in
both modes (AUDIO-QUALITY.md "The click and the recording's stems"). The limiter's work a block: the block's peak (kept while the samples go
into the delay), the larger of it and the last block's, the envelope, one divide if it moved; no loop over the
delay. Lengths other than 32 are build switches (`CLN_LA_B`, `CLN_LA_K`); AUDIO-QUALITY.md "The look-ahead" has the
measurements against the length and what it does and does not improve.

A change of Output while sound plays fades the chain's input to nothing over 16 blocks (11.6 ms), runs one more
silent block through the old chain (Clean's delay plays its last samples out), changes the chain at that zero with
both limiters at rest and the delay empty, and fades in (`out_fade`, `out_master`). Felucca's FX hold layer (`perf_master`, not in
ChoralRoot's unit) and Felucca's own builds always run Classic. `EMU_OUTPUT=classic` holds the emulator at Classic
whatever the settings say (`tools/emu/classic_exact.sh`).

## The master filter (0.22)

GitHub issue 16 (etyrnal, 2026-10-14): "Many of the patches contain a lot of harsh upper tones", and softening a pad,
clearing what competes with the bass, or leaving room for a singer wants "a simple single knob lpf/hpf". The owner's
answer there: a final EQ or band-pass in the FX section, apart from the engines; before the FX chain or after it?

**What it is**: a **low cut** and a **high cut** on the stereo master, the eighth page of the FX layer, **Filter**
(SELECT past Drive): Low cut (Off, then 20 Hz .. 2 kHz), High cut (200 Hz .. 16 kHz, then Off), Slope (6 or 12 dB an
octave) and Filter (Off / On) on KNOB 1 .. 4. Both cuts in is a band-pass. A new unit has it Off with both cuts at
their Off ends; **turning a cutoff switches the filter on** (and FX, as a send turned does), so the knob is heard at
once; KNOB 4 switches it off and on again with the cutoffs kept, to compare. FX off takes it out with the effects
(its cell reads Off). The four are the instrument's, like every effect parameter: no sound load touches them, the
global save keeps them (SETTINGS.md: tags 31 .. 34).

**Where in the chain, and why**: after the effects and before the master (`fx.c mix_block`: `xq_block` on the mix
after `xf_block`, before `out_master`). So the reverb's and the delay's wash is tamed with the parts (a high cut
softens the tail too, a low cut takes the reverb's mud with the chord's), and the limiter and the clip see what the
filter leaves: a low cut makes room under the limiter's threshold instead of being limited first. Before the
effects it would have left the wash as bright as it was and the limiter working on a signal the filter then changed.
The other place the request could go, **a cut per part** (in each part's chain beside its drive, before its sends),
is not built: it costs twice over for the same ear (both parts, every voice), has no effect on the wash, and the
issue's cases are all "the whole sound" cases; it stays an option if a player wants the bass full under a cut chord
(the issue reply says so). Nothing of it is in the engines: every engine keeps its own filter.

**The arithmetic** (`cr_fx.c xq_*`): a cut is one stage (6 dB) or two in a row (12 dB) of a one-pole, `y += k (x -
y)` with k in Q12, the low cut's stage `x` minus its low-pass. The knob reads the cut's -3 dB corner: two identical
poles are 3 dB down at 0.6436 of their own corner, so at 12 dB the poles sit at 1.554 x the knob. Every product keeps
its remainder (`xf_rs`, as every loop in the file): no dead band at a low k (a 20 Hz cut's k is 15 of 4096: without
the remainder its stage would stop 270 units short and leave a DC offset of that), no offset, exact zeros after
silence. The mix into a cut is held to +-3 x full scale (`XQ_MAX` 98304: the Classic limiter is 15 dB into its work
by then, the Clean one 11.5 dB; the products fit 32 bits with the second stage's difference at 4 x that). **Off
touches nothing**: with FX off, Filter Off, or both cuts at their Off ends, `xq_block` returns before it reads a
sample and its states are at rest: the master is bit for bit what it was (`cr_fx_test.c`: the defaults, FX off,
Filter Off, On with both cuts Off, and again after a cut was in). A cut switched in or out **fades**, its wet share
a straight line across each block over 16 blocks (11.6 ms, Options > Output's fade), the stages running from rest
under it; a cutoff turned **eases**, the k in use moving an eighth of itself towards the knob's each block (an
octave in 6 blocks: no zipper; the test sweeps the high cut from Off to 200 Hz and back under a tone, a detent every
20 ms, and no sample step exceeds the tone's own by 3 %). The slope eases the same way: the second stage's k goes to
4096 (y = x exactly) when 12 dB goes to 6 and the stage is skipped once there; 6 to 12 starts it at its input and
eases its k down. (An ease of a quarter of the way a block, the first try, stepped the second stage's slew by a
third at once on a slope change: a 1.5 x step under a tone. The geometric ease was the fix.) Only a cut that is in
costs anything.

**The cost on the unit** (the listing, `python3 tools/fx_loops.py build/choralroot.dis xq_cut`, at 205 instructions
a microsecond; the unit's `fx_*` will say, as for the effects: the fx stage's figure includes it, between `up` and
the master):

| a cut's loop, a sample a side | instructions | both sides, a half of 128 | us |
| --- | --- | --- | --- |
| low cut, 6 dB | 17 | 4352 | 21 |
| low cut, 12 dB | 24 | 6144 | 30 |
| high cut, 6 dB | 16 | 4096 | 20 |
| high cut, 12 dB | 22 | 5632 | 27 |
| both at 12 dB (a band-pass) | | 11776 | **57** |
| both at 6 dB | | 8448 | 41 |
| Off (the hash of four values, nothing else) | | ~40 | 0.2 |

One cut at 12 dB, the issue's case, is 27 .. 30 us of the half's 2902 (1 %); the band-pass at 12 dB is 57, the
"under 40" asked for only at 6 dB. The remainder is 2 of a stage's 6 instructions and the clamp 3 of a loop's: no
cheaper loop keeps the project's arithmetic. A fade's block costs the loop plus a copy and a mix (18 a sample).
The host's count (`cr_fx_test.c`, a guard): off 132 instructions a half, one 12 dB cut 1280, both 2560. XIP +1712 B
(the four loops, the fade, the block, the page), POOL +240 B (`xq`: the states and a fade's block of 32).

**Tests**: `tests/cr_fx_test.c t_filter` (above: Off bit-exact four ways and after a cut was in; each cut at three
corners x two slopes x three frequencies against the one-pole's own formula within 0.3 dB, both sides the same; both
cuts at their far ends with a square at the clamp and noise over it, bounded and exactly 0 within a second of
silence; the sweep and the toggles under a tone; the cost); `tools/emu/scripts/cr_fx_filter.txt` (the page, the
knobs, the cells, the session's four tags, and the WAV: a D major chord with the filter Off, then through the 200 Hz
high cut, its D4, F#4 and A4 at the formula's 53, 41 and 33 % of themselves, falling with the pitch);
`tests/cr_settings_test.c` (the block's four new entries took `rsv_fx`'s bytes, every other field where it was).

## The routing (0.27, GitHub issue 21)

Issue 21: "re-orderable FX ... like how in the original OEM firmware you could re-arrange the FX order". The owner's
answer there: a new FX page for routing that "shows the entire routing". The FX layer's last page, **Routing**:
1st, 2nd, 3rd (the chain's order; MANUAL.md section 8 has the knobs) and **Feed**.

**Why it is not a plain reorder**: ChoralRoot has no serial chain. Each part's drive runs before the mix; the chorus,
the delay and the reverb are three **parallel** mono sends whose stereo returns are added into the mix (`cr_fx.c
xc_block`, `xf_delay`, `xf_reverb`), and the reverb runs on core 1 while core 0 runs the other two (`fx.c
mix_fx_split`, DUALCORE.md "What each core runs"). Added in parallel, any order is the same sum. So the order
(`XP_ORDER`, 0 .. 5: `XF_ORD`'s six permutations; 0 is Chorus, Delay, Reverb) draws a chain over the three, and the
**feed** (`XP_FEED`, 0 .. 127) is how much of each effect's return goes into the **next** one's input besides the
parts' sends: position 1 into 2 and 2 into 3, never back (no loop can form; the first gets the sends alone). The
drive (per part, before the mix) and the master filter (on the master, after everything) are not in the chain, and
the page does not list them.

**Feed 0 is 0.26**, bit for bit: the default, and every unit updated hears nothing different until Feed is turned up.
`fx.c mix_block` calls the stage (`xg_feed`) only while Feed is up or a link's gain is still on its way down to 0
(one test of two words a block); with it not called, `xg.on` is 0 and no effect captures anything. With Feed 0 the
order is a picture.

**The previous block's return**: the feed an effect receives is what the one before it in the chain returned in the
**last** block (32 samples, 0.73 ms: no ear hears that as a delay, and it makes the chain's sound a hair's breadth
from a true series). That is what keeps the three free to run in any order and on either core: each effect, while
it runs, captures what it added to its output buffers as a mono block, (l + r) / 2 (`xg_pre` before its loop,
`xg_post` after: whatever else the buffers already held, the difference is its own), into a buffer of its own that
only the core running it writes; `xg_feed`, on core 0 at the effects stage's start (the parts mixed, the sends whole,
after the join of the block before), adds the links into this block's inputs before anything reads them. The
dual-core split is untouched: integer sums in a fixed order, so one core's samples and two cores' are the same
(`tests/cr_c1_thread_test.c` "routing": every pair at Feed 127, each at an order of its own, on a thread and all on
core 0, the same digest, and not Feed 0's).

**The rates**: a wet at the full rate (the chorus; the delay or the reverb when built at FS) is added into the next
effect's send at FS (a half-rate one down-samples it with its send). A half-rate wet into a half-rate effect (the
delay and the reverb in the default build) is added after that effect's `xf_down`, at the half rate and its input's
scale, through a hook (`xg.h`, `xg.hk`: `xf_delay` and `xf_reverb` add it when it holds something, and `xf_prep`
counts it as a send, so "nothing sent, tails over: nothing runs" stays right). A half-rate wet into a full-rate
effect (into the chorus) goes up through a half-band of its own (`xg.up`, one a link: `xf_up`, mono; cleared when the
link's source is silent or the order changes). No path is down-sampled or up-sampled twice.

**The gain**: Feed x 258 (Q15: 127 is 32766, unity less a hair), each link's gain in use gliding a 16th of the way a
block (`XG_GL` 4: 16 blocks, 12 ms; it lands) and a straight line across each block, so a detent never steps the
signal. A new order restarts both links' gains from 0: the new links fade in, and no block mixes the old topology
with the new. A wet is held to +-65535 (twice full scale) before its gain and a send it lands in to +-131068, the
down-sampler's own clamp.

**Measured** (`tests/cr_fx_test.c` t_route, the default build; the full-rate build the same in
`tests/run_cr_fx_rates.sh`):

- Feed 0 at each of the six orders renders a chord through the three effects as the build without the stage, every
  sample.
- Chorus > Delay > Reverb with a send into the chorus alone: Feed 0 leaves the delay and the reverb off, Feed 127
  brings both alive; Reverb > Delay > Chorus the mirror.
- A full-scale square into all three at Feed 127, every order: bounded (peak ~199700, 1.5 x full scale of the mix:
  well inside the master's clamp, no wrap), exact zeros and every effect off after the tail.
- Feed swept 0 -> 127 -> 0, a detent every 6 ms, under a held tone into the three: the largest second difference
  3.9 % of the largest step, the steady signal's own at Feed 127 (no click).
- **Cost**, host instructions a 128-frame half with all three running: no stage 58305, Feed 0 58296 (noise), Feed
  127 60796: +2500, 4.3 % (the full-rate build: 80941, 80976, 83799, +2823). On the unit that is the captures' two
  passes over each effect's block and the links' loops; by the file's ratio of host to unit instructions, a
  few tens of microseconds a half at most, unmeasured on the unit (`tools/emu/perf.sh` scenario (l) with Feed up is
  the measurement to take).
- **Memory**: 560 bytes of POOL (`xg`: three captures, two hooks, a link's scratch, two half-bands; 688 in the
  full-rate build).

**The session**: the order and the feed are the instrument's, as every effect parameter: two new tags of the FX block,
written only off their defaults (SETTINGS.md has the tags and what an older firmware does with them; the precedent is
the filter's tags of 0.22).

## The FX layer

SELECT steps eight pages; the picker shows the page's name (Sends, the effect, or Filter) over six marks, the label an
effect's page (`fx · on · 1/2`). The layer opens on Sends on a new unit and after a reset or a revert with nothing
saved; otherwise on the page it was left on (the session's `fx.sel`, a tag a page: `CSS_FXE_SENDS` is the new one,
6. A record written before it names an effect's page and opens there; an older firmware that reads a record naming
Sends does not know the tag and keeps Reverb 1).

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- |
| Sends | Drive (clip) | Chorus (mix) | Delay (mix) | Reverb (mix) |
| Reverb 1 | Size (room) | Damp (moon) | Decay (tail) | Send (mix) |
| Reverb 2 | Pre-delay (pre) | Mod (lfo) | Low cut (cut) | Width (width) |
| Chorus | Rate (lfo) | Depth (lfo) | Width (width; 0.21) | Send (mix) |
| Delay 1 | Time (echoes) | Spread (spread) | Feedback (echoes) | Send (mix) |
| Delay 2 | Sync (sync) | Mode (ping) | Tone (moon) | Wow (lfo) |
| Drive | – | – | – | Send (clip) |
| Filter (0.22) | Low cut (cut) | High cut (moon) | Slope (text: 6 dB / 12 dB) | Filter (text: Off / On) |

**Sends** (the owner, 2026-10-12: "I also really like in this sound editor page being able to set the send levels for
each FX, so I think when you enter the FX menu from main the first page should also show the 4 FX send levels for
convenience") is the sound editor's FX group on the layer's knob row: the same four, in its order, under its names,
on its knobs, with its step (6 a detent) and its read-out (per cent). It edits the **chord part's** sends, as every
Send cell of the layer does; the bass part's stay in the bass sound's editor. The Send cells on KNOB 4 of Reverb 1,
Chorus, Delay 1 and Drive are still there (the same values, read and stepped as on the Sends page: "One read-out, one
step" above), and KNOB 4 of the main view is still the send of the effect last picked (Reverb while the layer has
only shown Sends). While FX is off the four
read "off", as a Send cell; turning one switches FX on. Fixture: `design/make_fx_mockups.py` state 15.

The seven new glyphs (`cr_draw.c cr_picto`: `CR_G_TAIL PRE CUT WIDTH SPREAD SYNC PING`) are drawn in the manner of the
old ones and have fixtures (`design/make_fx_mockups.py` states 2, 3, 5b, 12, 13, 14; `tests/cr_draw_test.c`). The
designer app does not draw them yet: `design/choralroot-fm1-fx-screens.png` is stale for those states. `CR_G_SPRING`
went with the spring. The room (`CR_G_ROOM`, Size's alone, on Reverb 1; no other page, nor the Sends page or the
sound editor, draws it): the outer box is fixed (the cell's glyph box, at most 1.4 : 1) and **only the inner box, the
far wall, changes: 22 % of the box at Size 0, 70 % at 127**, about the centre, joined to the box by the four corner
lines; at its largest 4 px of background are left between the two (`tests/cr_draw_test.c` "room"). Its history: until
2026-10-11 the far wall SHRANK with Size, 70 % to 22 % (the owner: it "gets tighter"); the first fix (`1121f93`) made
the whole icon grow, 55 % to 100 % of the glyph box with the far wall 50 % to 42 % of it; the owner: "Currently it's
now scaling the whole icon. I don't want that. [...] I only want the inner box to change size. The inner box should be
small when the room size is small and large when the room size is large." So the drawing is the first one again with
the far wall's direction turned round.

## How to tune

- **Listen without a unit**: `python3 tools/emu/fx_review.py` renders one stab through nine settings into
  `build/fx-review/` (and through the old effects with `--old EMU`, an emulator built from 0.17), prints the levels and
  draws every WAV; `sh tests/run_cr_fx_rates.sh` the half / full pairs.
- **Returns**: `XD_WET`, `XD_WPP`, `XR_WET`. `cr_fx_test`'s level checks say where they stand against Felucca's.
- **Defaults**: the `XFP[]` table; the sends' in `cr_ui.c cu_sends_default`.
- **The delay's character**: the 40 Hz low cut and the Tone curve in `xf_coefs`, the feedback's top (`xfp * 255`),
  the glide (`6 + XD_SH` and the clamp of 128), the wow's two rates in `xf_block`, the knee in `xd_sat`.
- **The reverb's character**: `XR_LEN` (and `XR_BASE`, their running sum + `XR_GUARD` each, rounded up to a multiple
  of 16), `XR_AP` (and `XR_APB`: multiples of 16, each 16 clear of the one before; both asserted), the diffusers'
  5 / 8, the two modulation rates in `xf_block`, the Damp curve and the input gain's exponent in `xf_coefs`.
- **A parameter's range or meaning changes**: a new `CSS_FXP_*` tag (never renumber, never reuse), a line in
  `cr_settings.c CSS_FXG`.
- **A loop's cost**: `python3 tools/fx_loops.py build/choralroot.dis xd_run xr_run xf_up xf_down xf_block`; the
  sound against the build before: `python3 tools/fx_cmp.py BEFORE_DIR build/fx-review`.
- After any of it: `sh tests/run_cr_tests.sh`, `sh tools/emu/perf.sh`, `./build.sh` (the size line), and
  `BUDGET_UPDATE=1 python3 tests/target_budget.py build/choralroot.dis tests/target_budget.txt` if a loop grew on
  purpose.

## Decisions the owner did not make

1. **The delay at the full rate, the reverb at half** (above), from the emulator's numbers: the unit's counters may
   say otherwise, and both are one build flag.
2. **0.74 s a side** (a 32768-sample line: what the pool has).
3. **16-bit lines** for the reverb (32 bits: 48 dB less noise for the delay's last 0.15 s).
4. **`CR_FX_RAM` is off**: on the unit it changed nothing (reverb 345 us for 331). Kept as a switch. `CR_RAM_FX` (3y41) moves the
   whole stage, the chorus and the mix loops too: to be measured ("Code in RAM").
5. **A tail ends at one unit of the line** (-90 dBFS of it), nowhere above.
6. **"Inf" takes the damping out of the loop.**
7. **Nothing is saved while a tail rings**, with no time limit: with the reverb frozen, nothing is stored until it is
   turned down or FX is off; a power cut loses the changes since the last quiet moment. (The loops' own saves, at a
   loop's stop, are as they were and can still fall into a tail: `flash_holes` will show it. The save policy is
   being reworked separately.)
8. **The sends' defaults**: chord 35 / 45 / 25 / 0 (reverb, chorus, delay, drive), bass reverb 10.
9. **The editor's FX group stays**, now the part's sends; a change there is not an edit of the sound (but
   FM TONE's Drive, the sound's: it is).
10. **A sound saved from now on stores the part's sends** in its unused fields (FX off: zeros; an FM TONE sound's
    drive field is its Drive, and is used).
11. **The delay does not self-oscillate**: feedback tops out at 0.988.
12. **Spread makes the right side shorter**, 1 .. 1/2 of the left; synced it has four steps.
13. **Sync folds a division longer than the line to half** instead of clamping it.
14. **The wow is the same on both sides** (mono-safe).
15. **Ping-pong is the default mode**, at 1/8 synced with Spread 1:1; the default reverb is 3 s.
16. **The delay's time and the reverb's size glide (a pitch bend)** instead of crossfading.
17. **The delay's free Time has a new session tag** (25: 16 .. 740 ms; 9's 21 .. 1000 ms is retired): a unit that
    ran the first build starts at the default free time. Its other effect settings and its sends are kept.
18. **Felucca's goldens are untouched**: its effects stay in `fx.c` behind `CR_FX 0`.
19. **The engine picker's KNOB 4 does nothing now** (no parameter there was missing one); the record's `pick_roots`
    byte is reserved and written 0.
20. **The half's pending bit is cleared before the render** ("Late is not slow"): reasoned from the code and the
    unit's counters, not run on a unit. `CR_ACK_LATE=1` builds the old order.
21. **The shed counts how far behind its boundary a half started**, not only how long it took.
22. **The delay's moving read is linear**, not cubic: up to 2.4 dB less at 10 kHz a repeat with Wow on.
23. **The glides and the swings step once a block** (as ramps), not once a sample.
24. **The default build keeps the eight-line reverb with four diffusers**, about 10 us over its 150 us target by the
    count; `CR_FX_LITE=1` (two diffusers, two swinging lines, ~140 us) is the lighter one to hear beside it.
25. **`XR_BASE` and `XR_APB` moved** (the lines' and the diffusers' places in their rings; the lengths are the same):
    nothing is stored that depends on them.
26. **CZ-1 is as it was**: its options are listed, none built.
27. **Six more values in SysEx 78 PEAK** (30 for 24); a host that reads 24 sees the same 24.
28. **The master filter sits after the effects and before the master** (0.22, "The master filter"), not before the
    effects and not per part; **one-pole stages, 6 or 12 dB, no resonance** (a resonant two-pole with the same
    arithmetic was 55 us a side at 12 dB by the count); **turning a cutoff switches it on**; **its four session tags
    are written only off their defaults** (the FX block grew from 24 entries to 28 into `rsv_fx`; a firmware before
    0.22 reading a record with more than 24 takes that block's defaults, SETTINGS.md).
29. **The clamp at 3 x full scale** before a cut (`XQ_MAX`): a sample out there is already 15 dB into the limiter.
