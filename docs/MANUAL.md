# ChoralRoot FM-1: the manual

For ChoralRoot 0.26.1 on the M-VAVE FM-1 (what is new since 0.22 is marked so; 0.26.1 is 0.26 signed, [releases/0.26.1.md](releases/0.26.1.md); what 0.26 brought: [releases/0.26.md](releases/0.26.md)). It is written for someone who owns an
FM-1, has installed ChoralRoot with
the web installer and wants to play it. Nothing here needs the design documents; where one goes deeper, the manual
links to it.

How the manual names things:

- A button is named by its ChoralRoot job, with the label printed on the FM-1 in brackets the first time: **KEY**
  (printed SEL). The printed labels belong to the stock firmware and mostly mean something else here. The tables in
  [The panel](#3-the-panel) and the [quick reference](#18-quick-reference) carry both names.
- The knobs keep their printed names (MASTER, SELECT, PRESETS, ALGORITHM, KNOB 1 to KNOB 4), because the screen uses
  them too.
- Keys are named by their note. The leftmost key is F3, the rightmost G5.
- A **tap** is a short press. A **hold** is a press kept down past the hold time (0.4 s; Options > Hold Time).
- Text in `this type` is what the screen shows.

## Contents

1. [What ChoralRoot is, and is not](#1-what-choralroot-is-and-is-not)
2. [The first five minutes](#2-the-first-five-minutes)
3. [The panel](#3-the-panel)
4. [Chords](#4-chords)
5. [Layers](#5-layers)
6. [Bass](#6-bass)
7. [Performance modes](#7-performance-modes)
8. [FX](#8-fx)
9. [Tempo and metronome](#9-tempo-and-metronome)
10. [The looper](#10-the-looper)
11. [Sounds](#11-sounds)
12. [The sound editor](#12-the-sound-editor)
13. [Views](#13-views)
14. [Options](#14-options)
15. [MIDI and USB](#15-midi-and-usb)
16. [Updating and recovery](#16-updating-and-recovery)
17. [Troubleshooting](#17-troubleshooting)
18. [Quick reference](#18-quick-reference)
19. [Where to read more](#19-where-to-read-more)

## 1. What ChoralRoot is, and is not

ChoralRoot turns the FM-1 into a chord instrument in the manner of the Telepathic Instruments Orchid. The left end of
the keybed says what kind of chord you want, the rest of the keybed plays its root, and the instrument builds the
chord, voices it and writes its name on the screen. Around that it has a bass part that follows the chords,
performance modes (strum, arpeggio, patterns), effects, a looper that records chords, seven synth engines with a deep
sound editor, MIDI out on three channels and USB audio recording.

What it is not:

- **Not the stock firmware with additions.** ChoralRoot replaces the FM-1's firmware while it is installed. The stock
  sequencer, sampler and drum sounds are not in it; every sound is synthesised. The installer puts the stock firmware
  back whenever you want ([Back to the stock firmware](#back-to-the-stock-firmware)).
- **Not what the panel says.** Most printed labels have another job (SEL is KEY, ARP is PERF, ENV is BASS, and so on),
  and in the sound editor the buttons change again. A sticker sheet would carry the table of the
  [quick reference](#18-quick-reference).
- **No velocity.** The FM-1's keys do not sense it. One fixed velocity is set in Options.
- **No Bluetooth MIDI**, and the computer cannot play audio through the FM-1. USB MIDI works both ways, the TRS jack
  is a MIDI input, and the computer can record the FM-1 over USB.
- **Not affiliated** with Telepathic Instruments or M-VAVE.

You can try everything in this manual without the hardware: the firmware runs in the browser at
<https://quixotic7.github.io/ChoralRootFM1/emu/>.

## 2. The first five minutes

**Switch on.** The screen shows `choralroot` over three stripes, with the version under them for a moment. The logo
stays until you touch something.

**Play a note.** Press D4, the sixth white key from the left. Every key from C4 up to G5 is a **root key**. On its own
a root key plays one note; the screen shows `D` and under it `D4`.

**Play a chord.** The lowest four white keys and the three black keys between them are the **chord block**. Hold MAJ
(the black key A#3) and press D4: D major. The screen shows `DM` (a major chord has a capital `M` after its root, so
that it cannot be taken for the single note `D`) and the notes `D4 F#4 A4` under it, and the
three root keys where those notes sound light up. Now hold MIN (G#3) and m7 (the white key G3) together and press D4:

<img src="manual/chord.png" width="240" alt="The chord view: Dm with a small orange 7, the notes D4 F4 A4 C5 under it, C5 in orange">

The chord keys have to be down when the root goes down. The name stays on the screen after you let go.

**Free a hand with LOCK.** Tap the button printed **LFO** (LOCK, in ChoralRoot). It lights, and `lock` appears at the
top right. Hold MIN and let go: MIN stays lit and every root you play is now minor. Tap 9 (the white
key B3) to add a ninth, tap it again to take it away. Tap MAJ: major, with the extensions cleared. Tap LFO again to
switch LOCK off.

**Change the voicing.** Hold a chord and turn KNOB 1. Each click moves the lowest note up an octave (or, the other
way, the highest note down); the screen reads `voicing +2`.

**Open a layer.** Hold **FX** for about half a second. Its layer opens and stays open after you let go; the FX button
blinks.

<img src="manual/layer-fx-sends.png" width="240" alt="The FX layer on its first page, Sends: four cells Drive, Chorus, Delay, Reverb in the knob colours">

It opens on **Sends**: how much of the chords goes into each effect. SELECT steps on to the effects themselves, KNOB 1
to KNOB 4 turn the four cells under the name, and the root keys still play so you hear the
change. **OCT-** closes the layer (so do OCT+, HOME, and holding FX again). All the layer buttons work this way: a tap is the button's on / off,
a hold opens its layer. [Layers](#5-layers) has the rules.

**Change the sound.** Turn PRESETS: the next sound of the same engine loads at once, and the screen shows its place:
the engine in orange (`FM6`), the number big (`02`), the name (`FM BELL`), a stripe for every preset of the engine,
and `Hold OPT to change engine`. To change the engine, hold **OPT** (printed GLO) and turn PRESETS: the engines pass by
(FM6, FM TONE, VA, CZ-1, P5, WHEEL and, from 0.24-beta2, GRAIN). Let go of OPT and the engine you stopped on is loaded.

**Add the bass.** Tap **BASS** (printed ENV). A bass note now plays under every chord, and `Bass` shows at the top
right. ALGORITHM changes the bass sound. Tap BASS again to switch it off.

**Record a loop.** Tap **REC**. The REC light comes on, the top line reads `Rec · Overwrite` and a message says
`rec: play to start`. Play a few chords: recording starts with the first one. Tap REC again: `loop recorded`, and the
loop plays straight away. **LOOP** (printed PLAY) stops and starts it. Play over it, change the sound, turn KNOB 1:
the loop holds chords, not audio, so it follows. [The looper](#10-the-looper) has the rest.

**If anything hangs.** Press OCT- and OCT+ together: `PANIC`, every note off. (Holding the two does nothing more.
Holding them **with HOME** for five seconds is the way into update mode, see
[Power-on and held gestures](#power-on-and-held-gestures).)

## 3. The panel

### The keybed

27 keys, F3 to G5. The left end is the chord block (seven keys); the other 20 keys are the roots.

| Keys | ChoralRoot | Job |
| --- | --- | --- |
| F#3 G#3 A#3 (black) | **DIM MIN MAJ** | the chord type, while held |
| G#3 + A#3 together | **SUS** | both small black keys at once (one finger on both) |
| F3 G3 A3 B3 (white) | **6 m7 M7 9** | extensions, while held; they stack |
| C4 to G5 (20 keys) | **roots** | a single note alone, a chord with a chord key held or latched; OCT- / OCT+ shift them by octaves |

LOCK, which latches the chord block, is the **LFO** button (below).

```
black      DIM   MIN   MAJ         C#4   D#4         F#4   ...
           F#3   G#3   A#3
                 \__ SUS __/   (G#3 + A#3 together)
white    6     m7    M7    9     C4    D4    E4    F4   ...   G5
         F3    G3    A3    B3    |---------- roots: C4 .. G5 ---------|
```

In a layer, a menu or a dialog the root keys may take another job (they pick a slot, type a letter, choose an
engine). The chord block keeps its job everywhere but in the KEY layer, where MAJ and MIN pick the scale.

This is the **FM-1** layout, made for this keybed and the default since 0.25.1 (a unit updated from an older version
is moved to it once). **Options > Chord Keys** switches to **Classic**, the Orchid's layout: SUS on C#4, the 9 on C4,
LOCK on B3, the roots D4 to G5. See [The FM-1 chord keys](#the-fm-1-chord-keys) and
[The Classic chord keys](#the-classic-chord-keys).

### The buttons

Two rows of six, and the two octave buttons.

| Printed | ChoralRoot | Tap | Hold |
| --- | --- | --- | --- |
| FX | **FX** | the effects on / off | the FX layer |
| SEL | **KEY** | Key Mode on / off | the KEY layer |
| ENV | **BASS** | the bass on / off | the BASS layer |
| LFO | **LOCK** (Classic chord keys: **LATCH**) | LOCK on / off; GLO held, then LFO: LATCH on / off (Classic: LATCH on / off) | the LOCK layer (Classic: nothing) |
| EDIT | **EDIT** | the sound editor | the engine picker |
| GLO | **OPT** | the Options | with a knob: the knob's second job |
| HOME | **HOME** | back to the view; on the view: the next view | 0.6 s: the view menu |
| SAVE | **SAVE** | save the sound | held: **save everything** (`saved`). GLO + SAVE held 1 s: back to the last save (`reverted`) |
| ARP | **PERF** | performance on / off | the PERF layer |
| SEQ | **SEQ** | the loop editor, then the PLAY menu, then back to the view ([The loop editor](#the-loop-editor)). With OPT (GLO) held: metronome on / off | the SEQ key's hold menu (metronome) |
| PLAY | **LOOP** | play / stop the loop | the PLAY menu |
| REC | **REC** | record | undo the last layer |
| OCT- / OCT+ | octave | the roots an octave down / up; in layers, menus and dialogs: back / OK | both together: PANIC |

In the sound editor seven of the buttons become the editor's groups; see
[The sound editor](#12-the-sound-editor).

### The knobs

None of them can be pressed. A turn (of any knob but MASTER) shows its value large on the screen for about a second.

| Knob | Turn | With OPT held |
| --- | --- | --- |
| MASTER | the volume | |
| SELECT | the tempo, 20 to 300 BPM; in a layer or a menu: moves the picker (the KEY layer: turns its page) | the metronome's level |
| PRESETS | the chord sound: the next or previous one of its engine | the chord sound's engine |
| ALGORITHM | the bass sound: off, then the bass engine's sounds | the bass sound's engine (with BASS held instead: the bass level) |
| KNOB 1 | the chord's voicing (on the Full keyboard too, since 0.24) | the Single Notes split point |
| KNOB 2 | the bass register, in octaves | |
| KNOB 3 | the first parameter of the performance mode | |
| KNOB 4 | the chord part's send into the selected effect, in per cent | |

In a layer KNOB 1 to KNOB 4 are the layer's; in the editor they edit the four values of the active lane.

### The screen

- **Top left**: `Key: C` in yellow while Key Mode is on. While a loop records or plays: `Loop 1` in red (no key
  then).
- **Top right**: one status word, the first of these that applies: `Bass Solo`, the performance mode (`Strum`, `Slop`,
  `Arp`, `Pattern`, `Harp`), `Bass`, `Oct +1`, `Latch`, `lock`. While a loop records or plays, the looper's state
  instead (`Rec · Overwrite`, `Rec 2.3`, `Dub 3.2`), and a small dial at the right end that fills as the loop goes
  round.
- **The middle**: the chord and its notes ([Views](#13-views) has the other four views), or the open layer, menu,
  dialog or editor.
- **Knob read-outs** take the whole screen for about a second: the value large, a row of stripes, what it is
  (`120 / bpm`, `+1 / octave`, `34 / reverb`).
- **Messages** are a box over the screen for about a second (`loop recorded`, `saved FM6 26`, `no free slot`).
- **The footer** of a layer lists its keys (`SELECT: effect · FX: on/off · OCT-: back`; it uses the printed names).

The battery level shows only on the Options pages, at the top right.

After three minutes without a key, button, knob or incoming MIDI message the logo comes back, and after ten the
screen goes black, so that nothing stays on it long enough to leave a mark. Its backlight stays on, so in a dark
room a black screen may still glow a little. The lights, the looper and MIDI keep running. Any key, button, knob or
MIDI message wakes it and does its normal job at the same time.

### The lights

| Light | Means |
| --- | --- |
| KEY, PERF, FX, BASS lit | that function is on |
| LFO lit / pulsing | LOCK is on / LATCH is on (both: pulsing between lit and the middle level). Classic chord keys: LFO lit is LATCH, and B3 shows LOCK |
| SEQ and EDIT blinking together | the loop editor is open: either of them goes back to the PLAY menu |
| SEQ lit | the PLAY menu is open: SEQ leaves it |
| a layer button blinking | its layer is open |
| EDIT blinking | the sound editor (or the engine picker) is open |
| OPT lit | the Options are open (in the PLAY menu: Keys = Loops; in the KEY layer: its keys play; in the editor: SHIFT) |
| SAVE lit | the save dialog is open |
| REC lit / blinking | armed / counting in, recording or overdubbing |
| LOOP lit, its green light | the slot holds a loop, the loop is playing |
| OCT- lit, OCT+ blinking | in a layer, menu or dialog: back, OK (where there is nothing to confirm: close) |
| OCT- or OCT+ lit on the view | the roots are shifted down or up |
| chord keys | lit while held or latched (a latched SUS: G#3 and A#3); otherwise a faint glow, the lowest level (Classic: B3 lit while LOCK is on). In the KEY layer: MAJ and MIN glow, the selected scale's is lit, the others are dark |
| root keys | the notes of the sounding chord, by name: every root key with the name of a note that sounds is lit (a chord with a G in it lights both G keys), wherever on the keyboard the chord is voiced; the note a performance mode is playing blinks, on all its keys; a playing loop's notes glow dim the same way. With Key Mode on they show the key (below) |

**Key Mode on the keys.** The key lights have four levels: faint (an idle chord key), low (the glow: a note of
the scale, an idle button), middle (the tonic) and fully lit. With Key Mode off the root keys are dark: only the notes
that sound are lit (and a playing loop's notes glow), so the chord keys' faint row is all that shows on an idle
keyboard; on the Full keyboard all 27 keys are dark until played. With Key Mode on the roots show the key, wherever they play (on the views, in the PERF, FX and LOCK
layers, in the KEY layer too): the notes of the scale glow at the low level, the tonic's keys at a middle level, the
roots outside the scale are dark, white or black (they glow faintly when Snap makes them play: [Key
Mode](#key-mode)), and the notes that sound are fully lit over it. So a glance tells
you that Key Mode is on, which key it is, and where the tonic sits. The layers that use the root keys for something
else (the loop slots, the pickers, naming) show their own map instead. The Options are not one of them: the keys
play there and show what plays.

**The notes that sound.** While a chord sounds, every root key that carries the name of one of its notes is lit: D
major lights the D, F# and A keys in both octaves (there is one A). The octave does not matter, so a chord that
KNOB 1 has voiced far up or down, beyond the two octaves of the root keys, still shows on them, and so does a chord
moved by OCT- / OCT+. The lights show what sounds, not what you press: with Transpose at +5 in the KEY layer, D4
alone lights the two G keys. The chord keys never light for a note; they keep showing the chord type and LOCK. On
the Full keyboard all 27 keys are note keys, so every key of that name lights there. The bass part's own note does
not light the keys.

Options > LEDs chooses between Glow (idle keys glow dim, active ones are bright) and Stock (the other way round).
Stock has two levels only and is as it always was: idle chord keys and, with Key Mode off, every root key are lit;
with Key Mode on, the keys of the scale are lit, the tonic like the rest, and the keys outside it are dark.

## 4. Chords

### Types and extensions

Hold one chord type and any extensions, then press a root.

| Key | Name | Notes above the root (semitones) | On the screen (root D; the extensions on D major) |
| --- | --- | --- | --- |
| F#3 | DIM | 3, 6 | `Ddim` |
| G#3 | MIN | 3, 7 | `Dm` |
| A#3 | MAJ | 4, 7 | `DM` |
| G#3 + A#3 | SUS | 5, 7 | `Dsus` |
| F3 | 6 | adds 9 | `D` with a small `6` |
| G3 | m7 | adds 10 | `D` with a small `7` (on a sus chord: `m7`) |
| A3 | M7 | adds 11 | `D` with a small `M7` |
| B3 | 9 | adds 14 | `D` with a small `9` |

The keys are the FM-1 layout's, the default ([The Classic chord keys](#the-classic-chord-keys) has the other one: SUS
on C#4, the 9 on C4).

- **SUS is G#3 and A#3 together**, always: press both small black keys with one finger. A root pressed while the pair
  is still landing waits for it ([the SUS Window](#the-fm-1-chord-keys)), so a minor chord never turns into a sus
  chord under your fingers.
- With two types held, the one pressed last counts (unless it is a [secret chord](#secret-chords); G#3 + A#3 is
  always SUS).
- Extensions stack: MAJ + 6 + 9 reads `D` `6 9`. Three extensions read `JAZZ`, all four `WTF`.
- **A major chord on its own reads with a capital `M`: `DM`.** A minor chord has a small `m` (`Dm`). With an
  extension the major chord's `M` is left out, as chord names are usually written: `D` with a small `7`, `6`, `9` or
  `M7` (the major seventh, not "D major" plus a 7). The name is the same on every screen: the views, the PERF layer,
  the sequencer.
- A root with no chord key is a single note (unless Key Mode is on): it reads as the note's name alone, `D`.
- Under the name the screen lists the notes as they are voiced. Extension notes are orange.
- The name of the last chord stays on the screen until the next one.
- **The 6 with a seventh.** The 6 key adds the note 9 semitones above the root, right under a seventh (on D: B4
  under C5 or C#5). That is how the Orchid plays it and it is the default. **Options > 6th with 7th** set to
  **Octave up** plays that 6th an octave higher whenever the chord also has the 7 or the M7 (or both): D + 7 + 6 is
  then D F# A C and the B above them, a 13th chord. Chords without a seventh (`D` `6`, `D` `6 9`) and single notes do
  not change, on every chord type; the name on the screen does not change either. KNOB 1 voices the raised note as
  the top note it is (one step down brings it back inside). A recorded loop holds chords, not notes, so it plays by
  whichever value is set while it plays. The option acts from the next chord on: one that is sounding keeps its
  notes.

### Chord names

The names above are the Orchid's: they say which keys you hold. **Options > Chord Names** chooses between them and
the names a lead sheet would use for the notes that sound. Only the name on the screen changes: the notes, the
MIDI and a recorded loop are the same under either, and you can switch at any time.

| You hold (root D) | Orchid (default) | Standard |
| --- | --- | --- |
| MAJ | `DM` | `DM` |
| MAJ + 9 | `D` `9` | `Dadd9` |
| MAJ + 7 + 9 | `D` `7 9` | `D9` |
| MAJ + 6 + 9 | `D` `6 9` | `D6/9` |
| MAJ + 7 + 6 | `D` `7 6` | `D13` |
| MAJ + 7 + 6 + 9 | `D` `JAZZ` | `D13` |
| MAJ + M7 + 9 | `D` `M7 9` | `DM9` |
| MIN + 9 | `Dm` `9` | `Dmadd9` |
| MIN + 7 + 9 | `Dm` `7 9` | `Dm9` |
| SUS | `Dsus` | `Dsus4` |
| SUS + 7 | `Dsus` `m7` | `D7sus4` |
| SUS + 7 + 9 | `Dsus` `m7 9` | `D9sus4` |
| DIM + 6 | `Ddim` `6` | `Ddim7` |
| DIM + 7 | `Ddim` `7` | `Dm7b5` |
| MAJ + 7 + M7 | `D` `7 M7` | `D` `7 M7` |

In Standard:

- A 9 without a seventh is `add9`; with the 7 it is `9`, with M7 `M9`. The major seventh is always written `M`.
- A 6 with a seventh is a `13` (`D13`, `Dm13`, `DM13`), with or without the 9. The 6th still sounds where it
  always did, inside the octave: the name changes, not the voicing. To hear it as a 13th, an octave up, set
  **Options > 6th with 7th** to Octave up ([Types and extensions](#types-and-extensions)); the two options are
  separate, and either naming goes with either.
- SUS is the fourth, so it reads `sus4`, and a seventh or ninth goes in front: `D7sus4`, `D9sus4`.
- On a diminished chord the 6 key gives the diminished seventh (`Ddim7`) and the 7 key the half-diminished chord
  (`Dm7b5`).
- The [secret chords](#secret-chords): `D+` and `D5` as before; the power chord with a 9 is `Dsus2`, with a 7
  `D7no3`; `Dm` `add4` with extensions reads `Dm7add4`, `Dm9add4`.
- A major chord on its own still reads `DM`, because `D` alone is a single note.
- With **both sevenths** held (7 + M7) there is no such name: those chords keep the Orchid's (`7 M7`, `JAZZ`,
  `WTF`). Nothing else reads `JAZZ` or `WTF` in Standard.

The full list, with the notes of every chord, is in [CHORD-NAMES.md](CHORD-NAMES.md).

### LOCK

The **LFO** button switches LOCK on and off; LFO is lit and `lock` shows at the top right while it is on (with the
Classic chord keys LOCK is the key B3). A chord key pressed while LFO is down switches LOCK at that moment. With LOCK
on:

- the chord keys you are holding stay on when you let go (they stay lit);
- then, with no chord key held, a **type** key replaces the latched chord with that type alone, and an **extension**
  key switches its extension on or off without touching the type.

Example: hold MIN, let go: minor. Tap 6: m6. Tap 9: m6/9. Tap 9 again: m6. Tap MAJ: major, nothing else.

**Back to single notes.** With one type latched, press that same type key again on its own: the type goes off when
you let go, and the roots play single notes. The extensions you had latched are not cleared: they stay lit, do
nothing while there is no type, and are back the moment you press a type key again, whichever it is. So with MAJ and
M7 latched: MAJ again and D4 is a single D with M7 still lit; MAJ once more and D4 is Dmaj7 again. A second chord key
pressed while the type key is down cancels this (the keys held together latch together, as always); a root played
meanwhile does not. This is always so with LOCK on, whatever the two settings below say. (In Key Mode with Key plays
set to Chords a lone root is still the scale's chord, with the latched extensions.)

Switching LOCK off, or PANIC, clears what is latched. LOCK and what it latched are part of what a global save keeps
(SAVE held): the FM-1 then comes back with the same chord type and extensions lit (new in 0.18). LOCK holds chord
types, not keys, so it keeps what it latched when you change the layout (and when a unit is moved to the FM-1 keys
by an update): a latched SUS shows on G#3 + A#3. On the Full keyboard (Key Mode, below) the chord keys are notes; LOCK
on LFO works there too (Classic: B3 is a note there and there is no LOCK).

**The LOCK layer.** Hold LFO past the hold time (a tap is still on / off; a hold does not switch LOCK). The layer
stays open like the others, LFO blinks, and the top left says `lock · on` or `lock · off` (the bottom line:
`LFO: on/off`). The keys play as usual in it, so you hear what a setting does. OCT-, OCT+, HOME or holding LFO again
closes it (Classic: B3 for all of this). Two settings, both
remembered and both acting only while LOCK is on:

| Knob | Setting | Values |
| --- | --- | --- |
| KNOB 1 | **Type key** | `Clears` (default): a type key replaces the latched chord with that type alone, as described above. `Keeps`: a type key changes the type and leaves the latched extensions on, so with 6 and 9 on you can go from MAJ to MIN and keep them |
| KNOB 2 | **Ext keys** | `Toggle` (default): each extension key switches its extension on or off. `Pushed`: the extension keys work as a set, the way the type keys do. Press one or several together and let go: that is the set. The next extension key you press, with none held, starts a new set of just what you press then |

In Pushed, a second extension key pressed while the first is still down joins the set, and with two or more on,
pressing one of them on its own makes the set that one key. To switch the extensions off, press the only extension
that is on once more, on its own: it goes off when you let go (it keeps sounding until then, and if you add another
key while holding it, both stay on). Example: 6: 6 is on. 7: 7 is on, 6 is off. 7 again: nothing is on. 7, and 9
while holding it: 7 and 9. With `Keeps` and `Pushed` together a type key changes only the type and an extension key
only the extensions. With `Clears`, a type key that replaces another type clears the extensions as always; the
same type pressed again (off, above) and a type pressed when none is latched leave them alone.

### Voicing and octave

- **KNOB 1** revoices the chord: one click up moves the lowest note up an octave, one click down moves the highest
  note down, from -12 to +12. The screen adds `voicing +2` under the notes. The voicing applies to every chord that
  follows, and to a playing loop. On the Full keyboard too (since 0.24: chords play there, Key plays Chords and
  G5's chords); its octave is OCT- / OCT+'s.
- **OCT-** and **OCT+** shift the root keys by octaves, from -2 to +2 (`Oct +1` at the top right). A low voicing can
  put chord notes below D4, where no key can light for them; the notes line on the screen always has them.

### Play styles

Options > Play Style decides what the chord keys do while a root is already down.

| Style | A chord key pressed while a root is held | A chord key pressed with no root held |
| --- | --- | --- |
| **Simple** (default) | nothing: the chord was fixed when the root went down | nothing |
| **Advanced** | a type key replays the held chord as that type; an extension key switches its extension on or off | nothing |
| **Free** | as Advanced | the last root sounds again as that chord, for as long as a chord key is held |

Options > Extension Addition matters in Advanced and Free: **Add Note** (default) adds or removes just the
extension's note while the rest of the chord rings on; **Play Chord** strikes the whole chord again.

**With LOCK on** the chord keys mean the same in all three styles: the lit keys are the chord, and the LOCK layer's
`Type key` and `Ext keys` settings decide what a press does to them. The styles only differ in what follows the lit
keys. Simple: the next root you play. Advanced: also the roots you are holding, at once (a key that goes off leaves
the held chord, a new set replaces the old one, no type lit leaves a single note). Free: the same, and while you
hold a chord key with no root down, the last root sounds again as the lit chord; it stops when you let go of the
chord key, lit or not. A key that will go off when you let go (the one lit type or extension pressed again) still
sounds in that chord until then.

### Secret chords

Options > Secret Chords: Off (default), Simple (only while Play Style is Simple) or All. When it applies, type keys
held together make a third chord:

| Held (FM-1 keys) | Classic keys | Chord | On the screen |
| --- | --- | --- | --- |
| F#3 + G#3 + A#3 (DIM + SUS) | DIM + SUS | power chord (root, fifth, octave) | `D` with a small `5` |
| F#3 + A#3 | MAJ + SUS | augmented | `D+` |
| F#3 + G#3 | MIN + SUS | minor with a fourth instead of a fifth | `Dm` with a small `add4` |

The small `5` and `add4` are red. Extensions stack on top as usual.

### The FM-1 chord keys

The chord keys of this manual, the default since 0.25.1 (**Options > Chord Keys: FM-1**; new in 0.24, where Classic was
the default). Updating from an older version moves a unit to them once, whatever it had; after that the choice in
Options stays.

```
black      DIM   MIN   MAJ         C#4   D#4         F#4   G#4   A#4         C#5   D#5         F#5
           F#3   G#3   A#3
                 \__ SUS __/   (G#3 + A#3 together)
white    6     m7    M7    9     C4    D4    E4    F4    G4    A4    B4    C5    D5    E5    F5    G5
         F3    G3    A3    B3    |------------------------ roots: C4 .. G5 (20) ------------------------|
```

- **SUS is G#3 + A#3 held together**, always (press both small black keys with one finger).
- **The SUS Window** (Options > SUS Window, 30 to 80 ms, 30 by default). One finger never lands on both keys at
  once. A root pressed while the first of the pair is down and its window still open waits, and plays when the
  chord is decided: at once when the pair is complete, or when the window closes (then G#3 alone is MIN, A#3 alone
  MAJ). So you never hear a minor chord turn into a sus chord. A root with no type key down, or with type keys held
  from before, plays at once. A chord that is already sounding (Advanced, Free, or LOCK with roots held) changes
  once, at the same moment. In Simple a root pressed before the type keys stays a single note.
- **Secret chords** (only with Secret Chords on): F#3 + G#3 is m add4, F#3 + A#3 augmented, all three the power
  chord. With Secret Chords on, a pair waits for the window's close (F#3 could still join). With them off, those
  pairs are "the last pressed wins", and only G#3 + A#3 combine.
- **LOCK is on LFO**: tap LFO for LOCK on / off (the LFO light lit), hold it for the LOCK layer (LFO blinks; its
  bottom line says `LFO: on/off`). A key pressed while LFO is down switches LOCK at that moment. A latched SUS shows
  as G#3 and A#3 lit; that same pair pressed again on its own switches it off when you let go. In the sound editor
  LFO is the editor's, and LOCK cannot be switched there.
- **LATCH is GLO held, then LFO** (LFO held, then GLO, opens the LOCK layer). While LATCH is on the LFO light
  pulses; with LOCK on as well it pulses between lit and the middle level. Holding GLO on its own never opens the
  Options: only a quick click does. The screen's `Latch` and `lock` are as before.
- **The KEY layer** (keys on pick): G#3 and A#3 are its scale keys and nothing else; a one-finger pair picks once.
- **Picking keys** are on D4 to G5 in both layouts: the loop slots, the naming letters and the KEY layer's tonic;
  C4 and C#4 do nothing in those maps.
- Changing the layout lets go of every key held (a chord you are holding stops); what LOCK latched stays, and shows
  on the new layout's keys.
- Loops store chords, not keys, so they play the same in either layout.

### The Classic chord keys

**Options > Chord Keys: Classic** is the Orchid's layout (ChoralRoot's only one until 0.24, the default in 0.24 and
0.25). Everything else in this manual is the same; what moves:

| Keys | Classic | FM-1 |
| --- | --- | --- |
| F#3 G#3 A#3 | DIM MIN MAJ | the same |
| C#4 | **SUS**, a key of its own | a root (SUS is G#3 + A#3) |
| F3 G3 A3 | 6 m7 M7 | the same |
| C4 | **9** | a root (the 9 is B3) |
| B3 | **LOCK** (tap: on / off, lit; held: the LOCK layer) | the 9 |
| LFO | **LATCH** (tap) | LOCK (LATCH: GLO held + LFO) |
| roots | D4 to G5 (18) | C4 to G5 (20) |

```
black      DIM   MIN   MAJ         SUS
           F#3   G#3   A#3         C#4     D#4   ...
white    6     m7    M7    LOCK  9       D4    E4    F4   ...   G5
         F3    G3    A3    B3    C4      |-------- roots --------|
```

There is no SUS Window in Classic (SUS has its own key), and the secret chords are two types held together (DIM +
SUS, MAJ + SUS, MIN + SUS). On the Full keyboard B3 is a note, so there is no LOCK there.

### Key Mode

Tap **KEY** (printed SEL): the KEY light comes on and `Key: C` shows at the top left. Now a root key on its own plays
the chord that belongs on that note of the scale. In C major: D gives `Dm`, E `Em`, F `F`, G `G`, A `Am`, B `Bdim`.
A key outside the scale plays a sus chord on a neighbouring scale note (C# in C gives `Csus`). A chord key held
overrides the scale's choice of type. The root keys light the key: the scale's notes glow, the tonic's keys are
brighter, the keys outside the scale are dark ([The lights](#the-lights)).

Hold KEY to open the **KEY layer**:

<img src="manual/layer-key.png" width="240" alt="The KEY layer: a keyboard with the tonic lit yellow, and four cells Tonic, Scale, Transpose, Single">

| Control | In the KEY layer |
| --- | --- |
| any root key | that note becomes the tonic and Key Mode switches on. The scale stays as it is, unless MAJ or MIN is held with the root |
| MAJ, MIN (the chord keys) | the scale: Major or Minor (`Key: E minor`), at the press, with LOCK on or off. In this layer they are not chord keys: the chord you have latched with LOCK is untouched and is back when the layer closes. The other chord keys do nothing here; LFO is still LOCK (Classic: B3) |
| KNOB 1 | Tonic, step by step (Key Mode switches on) |
| KNOB 2 | Scale: Major or Minor |
| KNOB 3 | Transpose, -24 to +24 semitones. It moves everything you play from then on, in or out of Key Mode |
| KNOB 4 | Single: Full, Split Oct or Split Bass (below) |
| SELECT | turns the layer's page: page 1 is the four settings above, page 2 has Key plays, Keyboard, Snap and Numerals (below). The top line says `1/2` or `2/2`. SELECT does not change the tempo in this layer |
| KNOB 1, page 2 | **Key plays**: Chords or Notes |
| KNOB 2, page 2 | **Keyboard**: Chords or Full |
| KNOB 3, page 2 | **Snap**: Off, Scale or Chord |
| KNOB 4, page 2 | **Numerals**: Off, On or Solfège |
| OPT (printed GLO) tap | switches the keys between **pick** and **play** (below). It does not open the Options from here |
| OCT-, OCT+, HOME, KEY held again | close the layer |

A quick way to set a key: hold KEY and press a root at once (the layer opens straight away), then OCT- to close it.
For a minor key press MIN as well, before or after the root.

While the layer is open the chord keys' lights show the scale instead of the chord: MAJ and MIN glow, the one of
the selected scale is fully lit, and the six chord keys without a job here are dark. (With Options > LEDs set to
Stock, which has no half level, only the selected scale's key is lit.) Outside the layer the lights mean what they
always do.

**Keys: pick or play.** The layer opens with its keys on **pick**: the root keys set the tonic, MAJ and MIN the
scale, and nothing sounds. Tap **OPT** (printed GLO) in the layer and the keys **play** instead, exactly as outside
the layer: roots, chord keys, LOCK, with their usual lights. Now you can hear the key while KNOB 1 to KNOB 4 change
the tonic, the scale, the transposition and Single. The top of the layer says which it is (`key · keys pick`,
`key · keys play`), the footer shows the way to the other, and OPT is lit while the keys play. Tap OPT again for
pick. The choice is kept by a global save (SAVE held). OPT held with KNOB 1 is still the split point.

**Single: Full, Split Oct, Split Bass.** Single decides what the keys below the split point do.

- `Full`: nothing special.
- `Split Oct`: single notes (not chords) on keys whose note name lies below the split point sound an octave lower.
  The split point is F by default, so C to E are the lowered notes, in every octave; OPT + KNOB 1 or Options > Split
  Point moves it.
- `Split Bass`: two instruments side by side. The keys below the split point play the **bass part's sound** and
  nothing else, as single notes at their own pitch (the bass register, KNOB 2 on the view, shifts them by octaves);
  the other keys play the chord part and nothing else. With the usual keys that means the roots whose note name is
  below the split point (C to E by default) are bass notes and the others play chords as ever. While Split Bass is
  on the bass is played by hand: it does not follow the chords, and Bass on / off and the Bass Behaviour are left as
  they are and ignored until Single is changed back. The top right says `Split Bass`. The bass part is one voice.
  The looper records a bass-side note as a bass note: the loop plays it with the bass sound, also after you change
  Single ([The looper](#10-the-looper)).

**Key plays: Chords or Notes** (page 2, KNOB 1). `Chords` is Key Mode as described above: a lone root plays the
scale's chord. `Notes` keeps Key Mode as a guide and a guard only: the roots and the chord keys behave exactly as
with Key Mode off (a lone root is a single note, a chord key gives its chord), but a root outside the scale does
nothing at all: no sound, no MIDI, nothing in a loop, no light. The key still shows on the root keys. Snap (below)
makes those roots play instead.

**Keyboard: Chords or Full** (page 2, KNOB 2). `Chords` is the usual keybed: nine chord keys and eighteen roots.
`Full` turns all 27 keys, F3 to G5, into an ordinary chromatic keyboard playing single notes of the chord part:

- There are no chord keys. LOCK is on LFO and works here too; what you had latched is kept and is back, lit, when
  you return to `Chords`. (With the Classic chord keys there is no LOCK here: B3 is a note, and holding it does not
  open the LOCK layer.)
- **KNOB 1** on the view is the **voicing** (until 0.24 it was the octave here); the octave is OCT- and OCT+.
- **Options > Full K Shorts** On makes F#5 and G5 switches on the Full keyboard (F3 to F5 stay notes, 25 keys).
  **F#5** brings the layout's chord keys back on the low keys (the Chords keyboard, its roots up to F5); **G5**
  plays a chord on every key: the chord LOCK latched, else Key Mode's chord for the key, else (nothing latched, Key
  Mode off) single notes as normal. A quick press toggles; held past the Hold Time, a switch changes only while
  held. Neither plays a note. G5 starts on with Key Mode on and Key plays Chords (the chords every key played here
  before), off otherwise. Their lights: faint when off, lit when on.
- With Key Mode off the keyboard is chromatic. With Key Mode on the key shows on all 27 keys, and **Key plays**
  decides what they do: with `Chords` every key plays the chord that belongs on its note of the scale, as a lone
  root does on the Chords keyboard (in C major, D gives `Dm`, G gives `G`), and a key outside the scale its sus
  chord on a neighbouring scale note; the voicing you set on the Chords keyboard applies to these chords (new in
  0.22: until then the Full keyboard played single notes whatever Key plays said). With `Notes` the keys are single
  notes and the notes outside the scale do nothing, or play the nearest right note with Snap (below).
- The bass, the performance modes (an arpeggio of the notes you hold), the looper and MIDI out follow what you play
  as they follow a single note.
- The split is at the **second F**: F3 to E4 is the left side, F4 to G5 the right. `Split Oct` plays the left side
  an octave lower; `Split Bass` gives the left side to the bass part and the right side to the chord part. This
  split is fixed on the Full keyboard; the split point you set with OPT + KNOB 1 belongs to the Chords keyboard and
  is not changed by it.
- In the KEY layer with its keys on pick, the keys still pick: a root sets the tonic, MAJ and MIN the scale (the
  other low keys do nothing there). With the keys on play they are the Full keyboard.

**Snap: Off, Scale or Chord** (page 2, KNOB 3; off unless you turn it on). With Key plays `Notes`, on either
keyboard, a key outside the scale is a dead key: Snap gives it a right note to play, the way the Scaler app remaps
its keyboard, so every key under your fingers plays and none leaves the key.

- `Off`: as described above, a key outside the scale is silent.
- `Scale`: the key plays the nearest note of the scale. Between two notes of the scale it goes down, so it is
  predictable: in C major C# plays C, D# plays D, F# plays F, G# plays G and A# plays A.
- `Chord`: the key plays the nearest tone of the chord that is sounding right now, yours or a playing loop's,
  whichever way is nearer (over C major, D# plays E and A# plays C; midway between two tones it goes down), so a
  melody over a chord or a loop can hardly miss. When no chord sounds it does what `Scale` does. A tone of the
  sounding chord that is itself outside the scale, say the A flat of an F minor borrowed into C major, plays as it
  is.

A key inside the scale always plays itself. The snapped key behaves exactly as if you had pressed the key it was
moved to: the chord keys, the bass, the performance modes, the looper and MIDI out all see that note, and its
release ends it even if you change the key, the chord or Snap while holding it. With Key plays `Chords` the keys
outside the scale have their own job (the sus chord on a neighbouring scale note) and Snap changes nothing: its cell
is drawn grey then. The lights say what plays: with Snap on, the roots outside the scale glow
faintly instead of staying dark, and the note that sounds is lit on its own keys, so a snapped C# shows as the C keys
lit ([The lights](#the-lights)). Snap is kept by a global save (SAVE held), like the page's other settings.

**Numerals: Off, On or Solfège** (page 2, KNOB 4; off unless you turn it on). With Key Mode on, the Chord view shows the
Roman numeral of the chord that is sounding, in blue, centred above the chord's name: in C major `I` for C, `V` for G
(and for G7 and G9: the extensions stay with the name), `vi` for Am; in A minor `iv` for Dm. Capitals for a chord
with a major third (and for sus chords, power chords and single notes), small letters for a minor third; `°` after a
diminished chord, `+` after an augmented one. When nothing sounds there is no numeral, and the name stays where it
is. While an arpeggio or another performance mode steps through the chord, the name sits a little lower to make room
for the numeral. The other views (Keyboard, Notes, Geek Out, Scope, Geek Out 2) and the layers show no numeral.

The numeral counts from the tonic to the root **as you played it**: Transpose is left out. With Transpose +1 in C
major, the chord on C sounds as C sharp major; the name says `C#M` (what you hear) and the numeral says `I` (the
degree you played). So the tonic chord always reads `I`. A chord played back by a loop is counted with the Transpose
that is set now. A root that is not a note of the scale gets a `b` or a `#` (`bVII`); in Key Mode the keys cannot
play one, because a key outside the scale plays a chord on a scale note, so you will see one only when the key is
changed under a sounding chord or under a loop. Numerals changes nothing you hear.

**Solfège** shows the same degree as a solfège syllable in the numeral's place, with Do on the tonic whatever the
key (movable do): `DO RE MI FA SOL LA TI` for I to VII. In C major G is `SOL`, Dm `re`, Am `la`; in A minor Am is
`do`, Dm `fa` and C `MI` (the scale's third note, as its numeral is `III`). The syllable follows the numeral's rules:
capitals for a chord with a major third, small letters for a minor third (`SOL` for G, `sol` for Gm), `°` after a
diminished chord (`ti°`), `+` after an augmented one, a `b` or `#` before a root that is not a note of the scale
(`#FA`, `bTI`), and the tonic chord is `DO` at any Transpose. Fixed do, where Do is always the note C (the note
names used in Italy, France, Spain and elsewhere), is a different thing: this setting does not rename the notes or
the chord.

Key plays, Keyboard, Numerals and Single are kept by a global save (SAVE held), like the rest of the KEY layer.

The Geek Out view shows the transposition (`Trans +2`).

### LATCH

Hold **GLO**, then press **LFO** (the Classic chord keys: tap LFO, LATCH's own button there): what you play keeps
sounding after you let go, until the next thing you play replaces it. `Latch` shows at the top right and the LFO light
pulses. With a performance mode on, LATCH is what keeps an arpeggio running with your hands off the keys. GLO + LFO
again and the held notes stop. A performance mode's own **Hold** (Strum's KNOB 4, the arp's) lights LATCH too, since
it holds the sound the same way: LATCH's gesture then turns that Hold off and the notes stop; the next one latches as
usual.

LOCK and LATCH are different things: LOCK holds the **chord keys** for you, LATCH holds the **sound**.

### PANIC

OCT- and OCT+ pressed together: every note stops (on MIDI too: all notes off is sent), the octave goes back to 0,
the LOCK latch is cleared, LATCH goes off and a playing loop stops. A take that was being recorded is dropped; the
loop and the settings are kept. The screen goes red for a second and is then blank until you play.

## 5. Layers

A layer is a screen that borrows KNOB 1 to KNOB 4 (and sometimes the root keys) for one job. Six buttons have one:
KEY, PERF, FX, BASS, SEQ (its hold menu is the metronome's) and LOOP.

- **Tap = use.** A tap is always the button's on / off action, also while its own layer is open.
- **Hold = open, and it stays.** Hold the button past the hold time (0.4 s) and its layer opens and stays open after
  you let go. The button blinks. Touching a key or a knob while the button is down opens the layer at once.
- **OCT- or HOME = close.** So does holding the layer's own button again (the way you opened it). Holding another
  layer button switches straight to that layer.
- **The knob row.** Under the layer's picker, four cells show what KNOB 1 to KNOB 4 do, in the knob colours (blue,
  orange, white, green), each with a small picture of its value. The cell you just turned lights up. A dash means the
  knob has no job here.
- **SELECT** moves the picker at the top of the layer (in the KEY layer it turns the page; in the LOCK layer it stays
  the tempo).
- **OCT- is back, OCT+ is confirm.** Where a layer has something to confirm, OCT+ does it (the engine picker's
  sound, the save dialog). The layers below have nothing to confirm, so there
  OCT+ closes the layer just as OCT- does. On / off is always a tap of the layer's own button.
- The chord block keeps its job in every layer except KEY's, where MAJ and MIN pick the scale.

| Layer | Hold | The picker (SELECT) | The root keys | A tap of its button | OCT- / OCT+ |
| --- | --- | --- | --- | --- | --- |
| KEY | KEY (SEL) | a keyboard with the tonic lit | set the tonic (OPT tap: they play instead) | Key Mode on / off | close |
| PERF | PERF (ARP) | the performance mode | play as usual | performance on / off | close |
| FX | FX | the effect | play as usual | effects on / off | close |
| BASS | BASS (ENV) | the bass behaviour | the white keys D4 E4 F4 G4 pick the behaviour; nothing sounds | bass on / off | close |
| SEQ's hold menu (metronome) | SEQ | the time signature | D4 E4 F4 pick 4/4, 3/4, 6/8 | metronome on / off (in this layer only; on the view a tap is the loop editor) | close |
| PLAY menu | LOOP (PLAY) | see [The looper](#10-the-looper) | play, or pick loop slots | play / stop | close (in step entry: back a step / a rest) |

Three more things open the same way and are described in their own chapters: **EDIT** held is the engine picker
([Sounds](#11-sounds)), **HOME** held for 0.6 s is the view menu ([Views](#13-views)), and **SAVE** held is not a layer: it is the global save ([What the FM-1 remembers](#what-the-fm-1-remembers)). (Until 0.17 SAVE held showed the
loop slots for as long as it was held.)

PRESETS and ALGORITHM keep their jobs in a layer (except in the PLAY menu). The hold time is set in Options > Hold
Time (300, 400, 500 or 600 ms).

## 6. Bass

The bass is a second, monophonic part with its own sound. On a new unit it is off; from then on it is **on or
off as it was at your last save** (SAVE held) when you switch on (until 0.17 it was off at every power-on).

- **BASS** tap switches it on and off. The screen shows the bass sound's place, in orange (`VA` over `17`, then
  `DEEP SUB`; with Bass Behaviour Solo the top line reads `VA · solo`), or `00 / off`. Its bottom line,
  `Hold OPT to change engine`, is the way to another bass engine (below).
- **ALGORITHM** turns through off (`00 off`) and then the sounds of the bass part's engine. Its first sound is
  `00 INIT`, the engine's plain starting sound.
- **KNOB 2** moves the bass register, -2 to +4 octaves.
- **OPT + ALGORITHM** changes the bass sound's engine, exactly as OPT + PRESETS does for the chord sound: hold OPT
  (printed GLO), turn ALGORITHM until the engine you want is in the middle, let go. You land on the sound you last
  used in that engine. A bass that is off stays off. From 0.19 this works **wherever ALGORITHM alone is the bass
  sound**: on the main screen, in the Options, in every layer but the PLAY menu, and in the sound editor (in 0.18 it
  worked on the main screen only). Where ALGORITHM has another job, OPT does not change it into the engine picker:
  in the PLAY menu it stays the record mode, and in the loop editor OPT + ALGORITHM sets the step's Voicing to
  `Follow` or `0`. The table is in [Sounds](#11-sounds).
- **BASS held + ALGORITHM** is the bass level: hold BASS (printed ENV / BASS), turn ALGORITHM, let go. The screen
  shows `bass level`, and the bass stays on or off as it was. (Until 0.18 this was OPT + ALGORITHM.) If you have held
  BASS long enough for its layer to open, or the layer is open already, the same turn changes the layer's Level
  cell, and the layer stays open.

Hold BASS for the **BASS layer**:

| Control | In the BASS layer |
| --- | --- |
| SELECT, KNOB 1, or the white keys D4 E4 F4 G4 | Behaviour (below) |
| KNOB 2 | Register |
| KNOB 3 | Sound (as ALGORITHM, without wrapping round) |
| KNOB 4 | Level |
| ALGORITHM | the bass sound, as everywhere; **with BASS held: Level** |

The root keys do not sound while this layer is open, whatever its footer says; close it to hear the change.

| Behaviour | What the bass does |
| --- | --- |
| **Chords Only** (default) | plays the chord's root, in the bass register, under chords; a single note has no bass |
| **Unison Bass** | plays under everything, at the pitch of the key you pressed rather than in the bass register (KNOB 2 still moves it by octaves) |
| **Bass Single Notes** | plays the root under everything; a single note sounds on the bass alone |
| **Solo** | only the bass sounds; the chord sound is silent (`Bass Solo` at the top right) |

The same setting is Options > Bass Behaviour. The behaviour, the register and the level are kept by a global save (SAVE held).

To edit the bass sound, hold BASS and tap EDIT. To save it, hold BASS and tap SAVE. To change the bass part's
engine, hold OPT and turn ALGORITHM (above), or open the bass sound's editor and hold EDIT there
([Sounds](#11-sounds)).

## 7. Performance modes

A performance mode plays the notes of your chord for you. **PERF** (printed ARP) tap switches the chosen mode on and
off; the top right names it. Hold PERF for the **PERF layer**: SELECT picks the mode (picking one switches the
performance on), the root keys play so you hear it, KNOB 1 to KNOB 4 are the mode's parameters, and a PERF tap is
still on / off. OCT-, OCT+, HOME or holding PERF again closes the layer.

While an arpeggio or a pattern is playing, the top half of the PERF layer shows it: the chord's name, its notes on a
line and the sounding note hopping along, with the mode's name small at the top left (`Arpeggiate · on`). The four
cells under it stay as they are, and turning KNOB 1 to KNOB 4 does not disturb it. The mode picker always comes
first: for two seconds after the layer opens, and for two seconds after every turn of SELECT, the top half is the
picker with the mode's name large, so you can read what you switched to even while LATCH keeps the notes going.
When nothing is being stepped (no keys held, or Strum, Slop, Harp) the picker stays.

| Mode in the picker | What it does | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- | --- |
| **Strum**, **Strum 2 Octaves** | the notes one after another | Rate (ms) | Direction | Range (octaves) | Hold |
| **Slop** | a strum with uneven timing | Amount (%) | Rate | Direction | Range |
| **Arpeggiate**, **Arp 2 Octaves** | the notes in turn, in time with the tempo | Division | Direction | Gate (%) | Swing (%) |
| **Pattern** | one of twelve rhythmic figures over the chord's notes | Pattern | Division | Gate | Swing |
| **Harp** | a fast run over several octaves | Rate | Direction | Gate | Range |

- Direction: Up, Down, Up-down, Down-up, Played or Random.
- Division: 2/1, 1/1, 1/2, 1/4, 1/8, 1/16, 1/32 and the triplets of 1/2 to 1/32.
- Rate is in milliseconds at 120 BPM and follows the tempo.
- The two "2 Octaves" entries are the same modes with the range set to two octaves.
- The twelve patterns: pulse, alberti, waltz, cascade, updown, syncopate, gallop, skip, ballad, stride, drive, ladder.
  The layer shows the number; KNOB 3 outside the layer shows the name too.
- Strum's Hold keeps the strummed chord sounding after you let go, as LATCH does, and lights LATCH; LATCH's gesture
  (GLO held + LFO; Classic: a tap on LFO) turns it off again (0.22).

Outside the layer **KNOB 3** turns the mode's first parameter (the strum rate, the slop amount, the arp division,
the pattern, the harp rate). An arpeggio stops when you let go of the keys; switch LATCH on to keep it running.

On the Chord view, while an arpeggio or a pattern is playing, the screen shows it: the chord's name, its notes on a
line, and the note sounding lifted onto a block that hops from note to note. When the arpeggio stops the chord view
is back. Strum, Slop and Harp keep the chord view.

## 8. FX

Four effects: **Reverb**, **Chorus**, **Delay** and **Drive**, and from 0.22 a **Filter** on everything, after them
([The Filter page](#the-filter-page)). The reverb and the delay are new in 0.18: a large,
slow reverb that goes from a hall to a wash that does not end, and a stereo delay with separate times left and
right that can ping-pong. From 0.19 the chorus is stereo too: it comes back as two signals, one for each side,
moved by the same sweep a quarter turn apart, so on headphones it spreads out instead of sitting in the middle. Its
Rate, Depth and send are what they were. From 0.21 the Chorus page has **Width** on KNOB 3: 127 (where it starts) is
that full spread, 0 is one chorus in the middle, the same in both ears, and anything between is between. Width is
kept with the other effect settings when you hold SAVE. Turning Depth or Width while a sound plays is smooth: the
chorus slides to the new setting in a twentieth of a second (until 0.20, Depth clicked at every step).

The one rule to know: **the effects and their sends belong to the instrument, not to the sound.**

- A **send** is how much of a part goes into an effect. The chord part has four: into the reverb, the chorus and the
  delay, and its drive. The bass part has its own four. You set them once; **turning PRESETS, changing the engine or
  loading a saved sound never changes them**, so you can step through the sounds and hear each one in the same
  reverb and delay.
- Everything else about an effect (the reverb's size and decay, the delay's time and feedback, ...) is set once for
  the whole instrument too.
- **One exception: FM TONE's Drive belongs to the sound**, as on the Digitone. See
  [FM TONE's Drive](#fm-tones-drive) below.
- All of it is kept by a global save (SAVE held): [What the FM-1 remembers](#what-the-fm-1-remembers).

On the panel:

- **FX** tap switches the effects on and off: with FX off nothing is sent into them, from the bass neither. (An
  FM TONE sound keeps its Drive with FX off.)
- **KNOB 4** is the chord part's send into the selected effect (turning it switches the effects on). The selected
  effect is the one whose page the FX layer showed last; the reverb until you pick another. The screen shows the
  send in per cent, 0 to 100.
- Hold FX for the **FX layer**. SELECT steps through nine pages; the name of the page is big, an effect's page
  number is in the small line above it (`fx · on · 1/2`). KNOB 1 to KNOB 4 turn the four cells, a FX tap is still
  on / off, and the root keys play. OCT-, OCT+, HOME or holding FX again closes the layer.

| Page | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- |
| **Sends** | Drive | Chorus | Delay | Reverb |
| Reverb 1 | Size | Damp | Decay | **Send** |
| Reverb 2 | Pre-delay | Mod | Low cut | Width |
| Chorus | Rate | Depth | Width | **Send** |
| Delay 1 | Time | Spread | Feedback | **Send** |
| Delay 2 | Sync | Mode | Tone | Wow |
| Drive | | | | **Send** (the amount of drive) |
| Filter | Low cut | High cut | Slope | Filter (Off / On) |
| Routing | 1st | 2nd | 3rd | Feed |

**Sends**, the first page, is the chord part's four sends side by side: its drive, and how much of it goes into the
chorus, the delay and the reverb. They are the same four, in the same order and on the same knobs, as the FX group
of the sound editor ([The sound editor](#12-the-sound-editor)), and each is the same value as the **Send** cell on
the effect's own page: change it in one place and the others show it at once. **A send reads the same everywhere**:
in per cent, from `0%` (nothing) to `100%` (all of it), on the Sends page, in a Send cell, in the sound editor, and in
the large number KNOB 4 or a MIDI controller puts on the screen. A knob turned slowly moves a send by one (under
1 %), turned quickly by about 5 % a click, in each of these places ([Knob speed and OPT snap](#knob-speed-and-opt-snap)),
and both ends are reached exactly. A new FM-1 opens the layer on Sends;
after that it opens on the page you left it on. While FX is off the four cells read `off`; turning one switches the
effects on again. In the sound editor the FX group always shows the values that are kept, FX on or off.

<img src="manual/layer-fx.png" width="240" alt="The FX layer on Reverb 1: four cells Size, Damp, Decay, Send in the knob colours">

The Sends page and the Send cells are the chord part's. The bass part's four sends are in the bass sound's editor, FX
group (BASS held + EDIT, then the FX button); it plays into the same reverb and delay as the chords. A new FM-1 has a
little reverb on the bass and nothing else.

### The Filter page

The last page, **Filter** (0.22), is a low cut and a high cut on the whole sound, chords, bass and the effects' tails
together, placed after the effects and just before the output: a way to soften a bright patch, to take the low end
out of the chords so the bass has the room, or to leave a band open for a voice. It is not part of any sound: like
the effects it is set once for the instrument, every sound plays through it, and SAVE held keeps it.

- **Low cut** (KNOB 1): `Off` at the bottom, then 20 Hz up to 2 kHz. What is below the corner goes.
- **High cut** (KNOB 2): 200 Hz up to 16 kHz, then `Off` at the top. What is above the corner goes. Both in at once
  is a band-pass.
- **Slope** (KNOB 3): `6 dB` or `12 dB` an octave. 12 dB (where it starts) is the firmer cut; 6 dB is gentler, a
  tone control, and costs the unit half as much.
- **Filter** (KNOB 4): `Off` or `On`. Turning Low cut or High cut switches it On by itself (and FX on, if FX was
  off), so you hear the knob at once; KNOB 4 takes it out and puts it back with the cutoffs as you left them, to
  compare. With FX off the filter is off with the effects, and the cell reads `Off`.

A new FM-1 has the filter Off with both cuts at their `Off` ends, and with it Off nothing in the sound changes.
Turning a cutoff while a chord plays is smooth, and switching the filter or the slope fades over a hundredth of a
second: no click.

### Routing

The very last page, **Routing**, shows how the chorus, the delay and the reverb are joined. On the original
firmware you could put the effects in another order; this is where ChoralRoot does it. KNOB 1, 2 and 3 are the
chain from left to right, each cell the name of the effect in that place (`Chorus`, `Delay`, `Reverb` where a new
FM-1 starts). Turn a knob clockwise and its effect moves one place to the right, swapping with its neighbour;
anticlockwise, one place to the left. One click, one place.

**Feed** (KNOB 4, in per cent like a send) is how much of each effect goes on into the next one. At `0%`, where every
FM-1 starts, the three are side by side as they always were: each takes what the parts send it and nothing else,
and the order is only a picture. Turn Feed up and they become a chain: the first effect hears the parts, the second
hears the parts and the first effect's sound, the third the parts and the second's. Chorus into Delay into Reverb
makes the echoes shimmer and then washes them out; Reverb first gives a delay that repeats a whole hall. Nothing
feeds back the other way. Turning Feed does not switch FX on: it is a setting of the chain, the sends still decide
what goes in.

The drive and the filter are not in the chain and not on the page: the drive is part of each part's sound, before
everything, and the filter is on the whole output, after everything. Order and Feed are set once for the instrument
and kept by SAVE held, like the other effect settings.

### FM TONE's Drive

On FM TONE the Drive is part of the sound, like the Digitone's overdrive:

- **Each FM TONE preset has its own Drive.** Loading one sets it; saving a sound stores it. It is the **Drive** cell of
  the AMP screen in the sound editor (ENV group, fourth screen, second row), which reads 0 to 127 as on the Digitone.
  Of the factory presets only 10 WOBBLE has any (10); the others are at 0.
- **It sounds with FX off too.** FX off still closes the chorus, the delay and the reverb.
- The Drive on the Sends page, on the Drive page and in the editor's FX group is this same value while the chord
  part plays FM TONE (there in per cent: 127 is `100%`). With FX off that one cell stays lit and shows the value while
  the other three read `off`; turning it does not switch the effects on.
- Turning it is a change to the sound: the sound is marked as edited, and SAVE keeps it.
- The part's own drive, the one every other engine uses, is not lost: it waits, and is back as soon as the part plays
  another engine. So a driven FM TONE preset does not leave its drive on the next FM6 or VA sound.
- The bass part works the same way when it plays FM TONE.

An FM TONE sound you saved before this (0.14 to 0.17, or an earlier 0.18 build) loads with the drive the part had
at the moment you saved it. If FX happened to be off then, that is 0.

A saved sound still has four send values inside it (the format has not changed), but they are not used when it is
loaded.

### The reverb

| Cell | What it does |
| --- | --- |
| Size | how large the space is: the room grows from a hall to something much larger. In its picture, a room seen from the front, the outer box stays and the inner box, the far wall, is small for a small room and large for a large one. Turning it bends the tail's pitch for a moment, then it is still |
| Damp | how fast the highs die: low = bright, high = dark |
| Decay | how long the tail lasts: `0.3 s` to `72 s`, and at the very top **`Inf`**: the reverb holds what is in it (Damp is out of the way then). Turn it down to let go |
| Send | the chord part's send into the reverb |
| Pre-delay | the silence before the reverb answers, 0 to 138 ms: keeps the attack of a note clear |
| Mod | a slow movement inside the tail. With Mod up, a long tail loses its top first; **with Mod at 0, `Inf` holds everything** |
| Low cut | keeps the bass out of the reverb, 20 to 500 Hz |
| Width | 0 = a mono reverb, 127 = as wide as it goes |

The size does not change the decay time, and the decay does not make the reverb much louder: a long tail is longer,
not a different mix.

### The delay

<img src="manual/layer-fx-delay.png" width="240" alt="The FX layer on Delay 1: four cells Time, Spread, Feedback, Send; Time reads 1/8, Spread 1:1">

| Cell | What it does |
| --- | --- |
| Time | the time of the left side. Sync on: a note value of the tempo, `1/32` to `1/2`, with dotted (`1/8.`) and triplet (`1/8T`) values. Sync off: 16 to 740 ms. Clockwise is faster: a shorter time, the echoes closer together (as the cell's picture shows); anticlockwise is slower |
| Spread | the right side's time as a part of the left's. Sync on: `1:1`, `3:4`, `2:3`, `1:2` (with Time at 1/4: a dotted 1/8, a triplet, a 1/8 on the right). Sync off: `x1.00` down to `x0.50` |
| Feedback | how many repeats. Each repeat is a fixed part of the one before, down to silence: turned up, a short echo rings for many seconds and a long one for minutes |
| Send | the chord part's send into the delay |
| Sync | `Tempo` (the notes) or `Free` (the clock). Synced, it follows SELECT's tempo and a MIDI clock |
| Mode | `Stereo`: both sides repeat on their own. `Ping-pong`: the repeats cross from left to right and back |
| Tone | how dark the repeats get: each one is darker than the last |
| Wow | a slow wobble of the time, as a tape or an old echo has |

- Three quarters of a second (740 ms) a side is the longest time. A note value longer than that at the current
  tempo plays at half its length (a 1/4 at 60 BPM plays as a 1/8, a 1/2 at 120 BPM as a 1/4).
- Changing the time (or the tempo) bends the pitch of the repeats for a moment, as a tape echo does. It does not
  click.
- With Spread at `1:1` in Stereo mode, left and right are the same: one echo in the middle.
- Time and Spread each keep two values, one for Sync on and one for Sync off.

### After the last note

A tail dies away evenly, down to nothing; it is never cut. When nothing has been sent into them and their tails are
over, the reverb and the delay switch themselves off.

One thing follows from long tails. Storing stops the sound for a moment (about a twentieth of a second for each
thing written). What the FM-1 stores by itself, the Options, it therefore stores only when it is quiet, and a reverb
that is still ringing is not quiet: with Decay at `Inf` an Option you changed is not stored until you turn Decay down
or tap FX off. The global save (SAVE held) does not wait: it is your own press, and if a tail is ringing you hear
one to three short gaps in it ([What the FM-1 remembers](#what-the-fm-1-remembers)).

## 9. Tempo and metronome

- **SELECT** sets the tempo, 20 to 300 BPM. Arpeggios, patterns, the delay, the loop and the MIDI clock follow it.
- The click is switched on and off in the **PLAY menu**: hold LOOP, turn SELECT to page 2, **KNOB 2 is Metronome**,
  On or Off.
- From anywhere outside the sound editor, **hold OPT (GLO) and tap SEQ** for the same thing
  (`metronome on`). SEQ tapped by itself opens the loop editor (until 0.18 it was the click).
- Hold SEQ for **the SEQ key's hold menu, the metronome's**: SELECT (or D4, E4, F4) picks the time signature, 4/4, 3/4 or 6/8; KNOB 1 is the
  click's level. OPT + SELECT sets the level without opening the layer.
- The click is not part of the USB recording.

With Options > MIDI Clock set to In, the tempo follows the incoming clock instead ([MIDI and USB](#15-midi-and-usb)).

## 10. The looper

The looper records **what you play on the root keys, as chords**: which root, which chord, when and for how long. It
does not record audio. When the loop plays, the chords go through the instrument again, so the loop follows the sound
you choose now, the voicing on KNOB 1, the performance mode and the bass. The tempo changes its speed, never its
pitch.

**Bass notes played by hand.** With Single = `Split Bass` the keys below the split play the bass part, and the looper
records them as bass notes: the loop plays them back with the bass sound, and on the bass channel of MIDI out. Which
side a note is on is decided when you record it and kept with the loop: you can set Single back to Full, move the
split point or change the keyboard layout while the loop plays, and its bass line stays on the bass and its other
notes stay on the chord sound. That holds in every record mode, for steps entered in Step mode, after an undo, in a
saved slot and in a backup. The bass register (KNOB 2 on the view) still moves a looped bass line by octaves, as it
moves the bass keys; the chord voicing does not touch it. You can play bass by hand over a looped bass line: the
bass part is one voice, so the note played last is the one you hear. If you turn Split Bass off and switch the bass
on while such a loop plays, the bass follows the loop's chords and the recorded bass notes play as well, on the
same single voice. Loops recorded before 0.18 are unchanged: bass-side notes in them were stored as ordinary notes
and keep the chord sound.

### Recording and playing

| Button | What it does |
| --- | --- |
| **REC** tap | record, in the way the record mode says (below). While recording: ends the take. While armed or counting in: cancels |
| **LOOP** (PLAY) tap | play / stop. While recording: ends the take |
| **REC** held | undo the last layer (the screen shows how many layers are left); the first layer stays: `nothing to undo` |
| **GLO** held + **REC** | the next record mode (`Rec: Overdub`); it records nothing |
| **LOOP** held | the PLAY menu |

With the settings a new FM-1 has (length Free, mode Overwrite):

1. Tap REC. REC lights, the top line reads `Rec · Overwrite`, the message says `rec: play to start`.
2. Play. The first chord starts the take; REC blinks and the top line counts bars and beats (`Rec 2.3`).
3. Tap REC or LOOP. `loop recorded`: the loop plays at once, at the length you played.
4. LOOP stops and starts it. The loop is in memory now; **hold SAVE** to keep it over a power-off
   ([Slots and saving](#slots-and-saving)).

Tapping REC again starts over: the loop stops and REC is armed, and the old loop is replaced only when you play the
first chord. Tap REC once more before that and it is `cancelled`, the old loop intact.

The lights and the top line tell you where you are: REC lit is armed, REC blinking is recording, LOOP lit means the
slot holds a loop, its green light means it plays. The small dial at the right of the top line fills once per loop;
it carries a red dot while REC is armed (steady) or recording (blinking).

### The PLAY menu

Hold LOOP. The menu stays open like any layer (OCT-, OCT+, HOME or holding LOOP again closes it), and you can keep
playing and recording with it open.

<img src="manual/play-menu.png" width="240" alt="The PLAY menu: Loop 1, empty; ten slot boxes; the record mode Overwrite; the length Free; cells Length, Rate, Quantize, Count-in; the bottom line 1/2, keys: play, GLO: loops">

| Control | In the PLAY menu | Default |
| --- | --- | --- |
| PRESETS | the **slot**, 1 to 10 (the strip at the top: a filled box holds a loop; a small square in its corner: changes not saved yet) | 1 |
| ALGORITHM | the **record mode**: Overwrite, Advance, Overdub, Replace, Step | Overwrite |
| SELECT | the **page**: 1 or 2 (the bottom line shows `1/2` or `2/2`) | 1 |
| page 1, KNOB 1 | **Length** of the next take: Free, 1, 2, 4, 8 or 16 bars (also shown big in the middle) | Free |
| page 1, KNOB 2 | **Rate**: x1/16, x1/12, x1/8, x1/6, x1/4, x1/2, x3/4, x1, x1.5, x2, x4 | x1 |
| page 1, KNOB 3 | **Quantize**: none, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32 | none |
| page 1, KNOB 4 | **Count-in**: On or Off | On |
| page 2, KNOB 1 | **Level**: how loud the loop plays, 0 to 100 (0 mutes it) | 100 |
| page 2, KNOB 2 | **Metronome**: On or Off ([Tempo and metronome](#9-tempo-and-metronome)). KNOB 3 and 4 do nothing on this page | Off |
| OPT (GLO) tap | **what the keys do** in the menu: play, or the loop shortcuts (below). The bottom line says which. The Options do not open from here | play |
| EDIT | the **loop editor** ([below](#the-loop-editor)) | |
| SEQ | leaves the PLAY menu, back to the main screen (its light is lit while the menu is open) | |

While a loop records or plays, the middle of the menu is a timeline: the bars, a line of marks for each layer, the
playhead. These settings are kept by a global save (SAVE held), like the rest of how the FM-1 is set up to play.
(Until 0.17 SELECT set the length, Level was on KNOB 3 and the keys' job was also a cell on KNOB 4.)

- **Length.** Free: REC arms, the first chord starts the take, and it is as long as you play. With a number of bars,
  REC starts a one-bar count-in (the screen counts the beats down inside a red ring, with a click), then records
  exactly that many bars and plays by itself. Ending such a take early rounds it up to whole bars.
- **Rate** makes the loop run faster or slower against the tempo: x2 plays it twice in the time it took to record, x1/2
  takes twice as long. The loop stays locked to the clock: it does not drift, and a loop that is a whole number of
  bars long starts on a beat again at every rate but two (at x3/4 and x1.5 its start falls between the beats for some
  of its rounds, as three against four does). The slow end, x1/6, x1/8, x1/12 and x1/16, is for long loops: at x1/16
  the loop takes sixteen times as long, so a one-bar loop lasts sixteen bars. The pitch never changes; the sound, the bass, the performance mode and
  the effects are the ones of now. **Changing it while the loop plays takes effect when the loop next starts**, so
  nothing jumps or cuts off in the middle; stopped, it applies at once. The metronome, the count-in and MIDI clock out
  stay the tempo's. The dial in the top line and the timeline follow the loop's own time. If you record while the
  rate is not x1, the take is stored so that it plays back, at that rate, exactly as you played it; Length then counts
  the loop's own bars (at x2, a 4-bar take lasts 2 bars of the metronome).
- **Count-in** Off skips the count: with a number of bars the take then starts the moment you tap REC.
- **Quantize** moves the starts of the chords to the nearest grid point when a take you played ends. It has no part in
  Step entry or the loop editor, whose steps are always sixteenths: while the record mode is Step the cell reads
  `1/16`, greyed, and KNOB 3 answers `Step: always 1/16` (your Quantize setting is kept for the other modes).
- **The keys.** `keys: play` (the default): the keys play as usual with the menu open. Tap OPT for `keys: loops`
  (OPT lit): the white keys D4 to F5 pick slots 1 to 10, D#4 held for a second clears the loop (`hold to clear`,
  then `loop cleared`), F#4 is undo. Tap OPT again to play.

### Record modes

The record mode decides what a REC tap does when nothing is being recorded. You set it with ALGORITHM in the PLAY
menu, or from anywhere but the sound editor with a shortcut: **hold GLO and tap REC**. Each tap goes to the next
mode, Overwrite, Advance, Overdub, Replace, Step and round to Overwrite again, and a message names it
(`Rec: Overdub`). The shortcut never starts, arms or stops a recording, and in the loop editor it does not clear the
step (REC alone does that there). If you change the mode while you are recording, overdubbing or entering steps,
with the shortcut or with the knob, what you are doing goes on and ends as it began; the new mode counts from the
next REC.

| Mode | REC on an empty slot | REC when the slot holds a loop |
| --- | --- | --- |
| **Overwrite** | a new take | a new take that replaces the loop (it is replaced when the take starts) |
| **Advance** | a new take | moves to the next slot (after 10 comes 1), saves the loop it leaves, and records a new take there |
| **Overdub** | a new take | arms an overdub (it starts the loop if it was stopped): the next chord opens a new layer on top. REC ends the layer; LOOP ends it and stops |
| **Replace** | a new take | as Overdub, but while you hold a chord the older chords under it are taken out. Undo brings them back |
| **Step** | step entry | step entry (the loop there is replaced when you finish) |

While an overdub is armed or running the top line reads `Dub 3.2` (`Rep 3.2` for Replace).

**Step entry** writes chords one step at a time, with no clock running, the way a step sequencer does. **A step is a
sixteenth note: 16 steps are a bar** of 4/4 (12 in 3/4 and 6/8), and a pattern has up to **64 steps** (four bars). The
PLAY menu shows the pattern as rows of 16 steps, the filled ones red, the cursor outlined, and the cursor's step
number.

- Play a chord and let go: it is written on the cursor's step, one step long, and the cursor moves on. You hear the
  chord as you enter it.
- **Each new root is a new step.** You can keep the chord key (MAJ, MIN, ..) held down and tap one root after another,
  or latch the chord with LOCK and tap roots: every root you press after letting the last one go takes the next step.
- Keys held **together** go on one step: a second root pressed while the first is still down, or a bass note under a
  chord with Split Bass.
- OCT+ is a rest (the cursor moves on). OCT- goes back one step; nothing is erased.
- REC finishes and leaves the loop stopped (`steps recorded`); LOOP finishes and plays it.
- With Length = **Free** the loop is exactly the steps you entered: three chords make a loop three steps long, up to
  64. After the 64th the pattern is full: another chord is refused (`64 steps: full`).
- With a Length in bars the pattern has that many bars (1 bar = 16 steps, 2 = 32, 4 = 64) and the cursor wraps at its
  end. A pattern cannot be longer than 64 steps: with Length 8 or 16 bars, step entry uses 4 bars and says so (`Step:
  4 bars (64 steps)`). Takes you play by hand keep the full 8 or 16 bars.
- **Rate** scales the pattern like any loop: at x1 a step is a sixteenth, at x2 a thirty-second, at x1/2 an eighth.
  At the slow end a step is long: a dotted quarter at x1/6, a half note at x1/8, three quarters at x1/12, and **a whole
  bar at x1/16**, so 64 steps are 64 bars. A step's ratchet, its 1/4, 1/2 and 3/4 lengths and its condition stretch
  with it (at x1/16 a ratchet x4 plays four quarter notes in the bar).
- Quantize does nothing here (see above).

### The loop editor

On the main screen, tap **SEQ**. The loop in memory is shown as a pattern of steps, and you can
change it one step at a time. **SEQ goes round three places**: the main screen, the loop editor, the PLAY menu, and
the main screen again. So from the editor one tap of SEQ is the PLAY menu (the slots, the record mode, Length, Rate,
the metronome) and one more is back where you started. In the PLAY menu **EDIT** opens the same editor. (Outside the
PLAY menu EDIT is the sound editor, as ever.) SEQ does not open the editor from another layer, the Options or the
sound editor, and not while a take is being recorded (`end the take first`).

<img src="manual/loop-editor.png" width="240" alt="The loop editor, page Step: a row of sixteen steps, three red, the second outlined, a white bar along its foot and the next three; Am, step 2/16; cells Note len 4, Steps 16, Chance 100%, Velocity 100">
<img src="manual/loop-editor-trig.png" width="240" alt="The loop editor, page Trig: the same row, the second step with two ticks at its top and a blue strip at its right; Am +2 under the grid; cells Ratchet x2, Condition 1:2, Voicing +2, Transpose A4">

The grid is the same as in Step entry: **sixteenth-note steps, 16 to a row, four rows: all 64 steps of a pattern on one
screen.** A red step has a chord or a note starting in it. A dark red step has nothing starting in it, but a longer
note from an earlier step is still sounding there. Grey steps are empty. The outlined step is the selected one (the
outline pulses, bright and dim); its chord is named under the grid, and if the step has a Voicing of its own, that
follows the name in blue: `Am +2`. **That name is always the selected step's**, whether the loop plays or not; only
while you hold keys down does it show the chord you are playing instead. While the loop plays, the step being played
lights up white, and **the chord or note the loop is playing is named in blue at the left of the page's name**
(`Step` or `Trig`, which stays where it is). It stays there until the next step that sounds, and goes when the loop
stops.

What a step shows:

- **The white bar is the selected step's length.** It starts on the selected step and runs along the foot of every
  step its notes sound in: one step, one cell; four steps, four cells, on into the next row and round the loop's end
  if the note is that long. A Note len of `1/4`, `1/2` or `3/4` is a bar a quarter, half or three quarters of the cell
  wide. Only the selected step has a bar. An empty step has none.
- **The colour is the velocity.** Red is the usual 100. Softer steps go darker and cooler, down to plum; louder ones
  go lighter, up to peach.
- **A strip down the right side is the Chance.** Yellow, and dimmer the less likely the step is: bright at 90 %,
  nearly black at 10 %. At 100 % there is no strip. **If the step has a Condition the strip is blue** (and dimmer with
  the Chance in the same way).
- **Small ticks along the top are the Ratchet**: two, three or four of them.

| Control | In the loop editor |
| --- | --- |
| PRESETS | the **step**: the cursor, a step a click, round and round. The screen shows `step 2/16` and the chord in it. **While the loop is stopped, the step you land on plays**, as the loop would play it (its length, its voicing, its ratchet), so you can hear your way along the pattern; an empty step is silence, and the next click ends what is sounding. While the loop plays you hear the loop, not the step. Nothing is recorded by this. **GLO held + PRESETS** jumps 8 steps a click. (PRESETS never changes the sound or the slot while the editor is open) |
| SELECT | the **page**: `Step` and `Trig`. The page's name is at the top, the bottom line begins `1/2` or `2/2` |
| ALGORITHM | the selected step's **Voicing**, on either page. **GLO held + ALGORITHM** jumps between `Follow` and `0`: from either one a click gives the other; from any other value a click clockwise gives `0`, the other way `Follow`. (On the main screen GLO + ALGORITHM is the bass sound's engine; in the editor it is this) |
| the keys | play a chord or a note: it **replaces** what is in the selected step, and you hear it. Keys pressed together all go into the step. **Only what you play goes in**, also while the loop plays: never the loop's own notes. The step has the chord as soon as you let the keys go, with LATCH on too. The cursor stays on the step; turn PRESETS to move on |
| OCT-, OCT+ | the **octave**, as on the main screen: what you play into a step next is entered there |
| REC tap | **clears** the selected step, its notes and its settings (REC does not record here) |
| **Page 1, Step** | |
| KNOB 1 | **Note len**: how long the selected step's notes sound: `1/4`, `1/2` or `3/4` of a step, then 1 to 64 steps. **Nothing else moves**: the other steps stay where they are and the loop keeps its length. The white bar along the foot of the selected step and the steps after it shows how far the note reaches |
| KNOB 2 | **Steps**: the loop's length, 1 to 64 steps, a step a click. Shorter hides the last step, longer brings it back; past what there was, longer adds an empty step. **GLO held + KNOB 2** goes straight to the next of 8, 16, 32, 48 and 64 steps (in 3/4 and 6/8: 12, 24, 36, 48) |
| KNOB 3 | **Chance**: how often the step plays, 100 % down to 10 % |
| KNOB 4 | the step's **Velocity** |
| **Page 2, Trig** | |
| KNOB 1 | **Ratchet**: Off, x2, x3, x4: the step's notes are struck two, three or four times within the one step |
| KNOB 2 | **Condition**: on which passes of the loop the step plays: Always, `1:2` `2:2`, `1:3` `2:3` `3:3`, `1:4` .. `4:4`, `1st`, `not 1st` |
| KNOB 3 | **Voicing**: Follow, or a voicing of its own, -12 to +12 (the same as ALGORITHM; with GLO held: `Follow` / `0`) |
| KNOB 4 | **Transpose**: moves the selected step's notes up or down, a semitone a click. A C major chord one click up is C# major. The cell shows the step's root note (`C#4`), and while you turn, how far you have moved it (`+1`) |
| LOOP tap | play / stop, as ever, so you hear the edits in place. The cursor does not follow the playback; the white step does |
| REC held | the looper's undo (see below) |
| SEQ, EDIT (tap or hold), LOOP held | close the editor, back to the PLAY menu. Nothing to confirm: an edit is done when you make it. The bottom line says `SEQ: back` |
| HOME | closes the editor and the PLAY menu: the main screen |

**Ratchet, Condition, Chance and Voicing belong to the step**: to everything that starts in it, the chord and, with
Split Bass, the bass note. The grid shows them: ticks for the ratchet, the strip for the chance and the condition, the blue
number after the chord for the voicing (above). An empty step has none of them (the cells show `-`); clearing a step clears them; playing new notes
into a step keeps them.

- **Ratchet.** x2, x3 or x4 strikes the step's notes that many times, evenly spaced inside the one step. Each strike
  is as long as its share of the step; with a Note len of `1/2` it is half of that, so the strikes are short with gaps
  between them. **With a Note len longer than one step, the last strike is held to the end of the note.** Ratchet x2
  with Note len 2: a note half a step long, then one a step and a half long. Ratchet x4 with Note len 4: three notes
  a quarter of a step long, then a fourth that lasts three and a quarter steps. The held note sounds over the steps
  after it, as any long note does.
- **Transpose.** It changes the notes stored in the step: every chord and note that starts in it, the bass note too,
  moves by the same number of semitones. Nothing else changes: the chord type, its length, its Ratchet, Condition,
  Chance and Voicing stay. It always moves by semitones, also with Key Mode on (Key Mode decides what the keys play,
  not what is already in a loop). No note can go below the lowest or above the highest MIDI note: a click that would
  take one there does nothing (`no higher`, `no lower`). There is no undo for it; turn it back.
- **Condition.** `1:2` plays on the first of every two passes of the loop, `2:2` on the second; `1:3`, `2:3`, `3:3`
  and `1:4` .. `4:4` the same over three and four passes. `1st` plays only the first time round, `not 1st` every time
  but the first. The passes are counted from the moment the loop starts playing: stop and start, and it is pass 1
  again.
- **Chance.** 100, 90, 75, 60, 50, 40, 25 or 10 %: each pass, the step plays or stays silent by that chance. The
  whole step decides together: its chord, its bass note and all the strikes of its ratchet. With a Condition as well,
  both have to say yes.
- **Voicing.** `Follow` is the voicing as it is set when the loop plays (KNOB 1 on the main screen), as every loop
  plays. A number gives that step's chords their own voicing, whatever the live one is. Single notes and bass notes
  have no voicing, so it does nothing to them.
- **These four are kept for the first 64 steps of a loop.** In a take longer than that, the steps on its later pages
  show `-` for them.

Things to know:

- **A long note sounds over the steps after it, and those steps still play.** A chord four steps long with another
  chord on the next step: both sound together for three steps, as if you had held one and played the other. (The loop
  plays up to eight chords at once; a ninth takes the place of the one that would end first.) A note longer than the
  rest of the loop sounds on across the loop's start.
- **A chord played on a step keeps that step's note length.** Set a step to 4 steps long, then try other chords on it:
  each is 4 long. On an empty step a chord is 1 step long.
- **A loop you played by hand** opens too, at any Quantize setting. Its chords are shown on the sixteenth grid: a chord
  belongs to the step it starts in. **Nothing is moved**: a chord you played a little early or late keeps its exact
  timing until you replace that step (the new chord starts on the step). Its Note len shows the steps it reaches into
  (a part of a step counts as one, unless it is exactly a quarter, a half or three quarters of a step); turning
  KNOB 1 to a whole number makes it end on the grid.
- **A loop longer than 64 steps** (a long take) is shown 64 steps at a time; the cursor walks through all of it and the
  screen follows (`step 70/128 · p2`). KNOB 2 can make such a loop shorter, not longer.
- **A step loop made with an earlier 0.18 test build**, whose steps were as long as the Quantize value, plays as it
  did. Here it shows on the sixteenth grid: quarter-note steps are a chord on every fourth step, 4 steps long.
- **An empty step**: its Note len, Chance and Velocity cells, and the four on the Trig page, show `-`.
- **An empty slot** opens as an empty pattern of the set Length (Free: one bar, 16 steps; 8 or 16 bars: 64 steps).
  Leave without entering anything and the slot is empty again.
- **Split Bass:** a note played on the bass side replaces the step's bass notes only, a chord on the other side only
  the chords, as when recording.
- **Steps hidden by KNOB 2** are kept only while the editor is open: when you close it they are gone. A save while it
  is open writes the loop as it sounds.
- **Undo.** There is no undo of a single edit. The first edit also folds the loop's layers into one (what a Replace
  layer had taken out is gone for good), so after an edit REC held says `nothing to undo`. The way back is the last
  save: **GLO + SAVE** brings the saved loop back. Opening the editor and closing it without an edit changes nothing.
- Edits are in memory, like everything you record: hold SAVE to keep them.

### Slots and saving

There are ten slots. One loop is loaded at a time, and it lives in memory: **recording, overdubbing, undoing, clearing
and editing change the loop in memory, not the slot.** The slot is written in two cases only:

- **SAVE held** (the global save, [What the FM-1 remembers](#what-the-fm-1-remembers)): the loop goes to its slot
  with everything else. The PLAY menu shows a small square in the corner of a slot whose loop has changes not saved
  yet.
- **When you move to another slot** (PRESETS in the PLAY menu, a white key with the keys on loops, or REC in
  Advance mode): the loop you leave is saved to its slot first (`loop 2 · saved 1`), so a slot keeps what was
  recorded in it. At once when the loop is stopped; when it plays, the switch happens at the end of the cycle
  (`loop 2: next cycle`) and the loop left is written when the loop next stops.

So if you record over a loop you liked, it is not lost: **GLO + SAVE** held for a second brings back the loop as it
was last saved, and so does switching off and on. And nothing is written while you record or play, so the looper
never interrupts the sound by itself. (Until 0.17, and in the first 0.18 builds, a loop was saved every time it
stopped.)

- Clearing a loop and saving (or leaving the slot) empties the slot.
- SAVE held while a loop plays or a take is open waits: `saves at stop`, and it is written when the loop stops.
- The SAVE-held `Loops` screen of 0.17 (Save / Load / Delete on the white keys) is gone. To copy nothing is needed
  any more; to empty a slot, clear its loop and save.

Limits: 512 chords in a loop over at most 32 layers; reaching the limit ends the take (`loop full`).

With MIDI Clock set to Out the FM-1 sends start and stop with the loop; set to In, the loop follows incoming start,
continue and stop.

More detail: [LOOPER.md](LOOPER.md), [LOOPER-MODES.md](LOOPER-MODES.md).

## 11. Sounds

ChoralRoot plays two sounds at once: the **chord sound** (PRESETS) and the **bass sound** (ALGORITHM). Each belongs
to one of seven engines (six before 0.24-beta2).

### The engines

| Engine | What it is | Factory presets |
| --- | --- | --- |
| **FM6** | six-operator FM that plays DX7 voices | 25 |
| **FM TONE** | four-operator FM in the style of the Digitone | 16 |
| **VA** | ChoralRoot's virtual analog: four oscillators, a morphing filter, four envelopes, four LFOs, a modulation matrix, stereo | 25 |
| **CZ-1** | Casio CZ-1 phase distortion, with Casio's own 64 tones | 64 |
| **P5** | a Prophet-5 model (Melodee's engine; new in 0.22): two oscillators with sync and Poly-Mod into a four-pole filter, two envelopes, the wheel's modulation; Sequential's own programs | 115 |
| **WHEEL** | a tonewheel organ with nine drawbars | 5 |
| **GRAIN** | a granular synth (new in 0.24-beta2): clouds of grains read from two sources, A and B, that it makes itself from four knobs each, MORPH between them; the VA's filter, two envelopes, two LFOs and a modulation matrix | 8 |

This is the order in which the instrument shows them. Up to 0.17 there were six more, PHASE, LOFI, VOICE, TRIO,
NOISE and PHYS; they are retired (see [Sounds from outside](#sounds-from-outside) for what happens to a sound saved on one). At power-on the chord sound is the one of your last global save (on a
new unit FM6 `TINE EP`), and the bass sound is VA `DEEP SUB` until you choose another.

### Presets: one pool per engine

Every engine has one list, its **pool**:

- `00 INIT`, the engine's plain starting sound;
- then the factory presets;
- then the sounds you added.

<img src="manual/sound-popup.png" width="240" alt="The sound read-out: FM6 in orange over 02, FM BELL, two of the 25 stripes lit, Hold OPT to change engine">

**PRESETS** turns through the pool of the chord sound's engine and wraps round. The read-out is the engine's name
in orange, the place big under it, and the sound's name. The stripes are the presets of the pool (`00 INIT` is not
one): none is lit on INIT, all of them on the last preset, so they show how many there are and where you are. A
small square after the name marks a factory preset you have saved over. The bottom line says how to get to another
engine: `Hold OPT to change engine` (hold OPT, printed GLO, and turn PRESETS). **ALGORITHM** does the same for the
bass, with `00 off` in front of the pool; its read-out is all orange and its bottom line is the same,
`Hold OPT to change engine` (hold OPT and turn ALGORITHM: the bass part's engine). The line is there wherever
holding OPT does change the engine, which from 0.19 includes the layers. Where it does not (the PLAY menu, the sound
editor's picker, the save dialog, and for the chord sound the sound editor) the bottom line is the engine and the
place instead (`FM6 · 02/25`) and nothing stands over the number.

When you step to another preset the stripes move from where they were: going up, the ones in between light one after
another; going down, they go out. Only the first read-out after switching on, and the first after changing the engine,
fills from the left. A long way (from the last preset round to INIT, say) takes under half a second.

**OPT + PRESETS** changes the chord sound's engine: hold OPT, turn PRESETS until the engine you want is in the
middle (the screen shows how many presets it has), let go. You land on the sound you last used in that engine.
**OPT + ALGORITHM** does the same for the bass.

The rule, the same for both knobs (0.19): **wherever the knob alone changes the sound, OPT held makes it change the
engine; where the knob has another job, OPT leaves that job alone.**

| Where | PRESETS alone | OPT + PRESETS | ALGORITHM alone | OPT + ALGORITHM |
| --- | --- | --- | --- | --- |
| the main screen (any view), the Options | chord sound | **chord engine** | bass sound | **bass engine** |
| a layer: KEY, PERF, FX, BASS, SEQ (metronome), LOCK, the view menu | chord sound | **chord engine** (new in 0.19) | bass sound | **bass engine** (new in 0.19) |
| the sound editor | opens the editor's preset picker | the same (the editor's picker) | bass sound | **bass engine** (new in 0.19) |
| the PLAY menu | the loop slot | the loop slot | the record mode | the record mode |
| the loop editor | the step | 8 steps a click | the step's Voicing | Voicing `Follow` / `0` |
| the editor's picker (EDIT held, or PRESETS in the editor) | its presets | its presets | bass sound | bass sound |
| the save dialog | chord sound | chord sound | bass sound | bass sound |

In the editor's picker and on the save dialog OPT changes nothing about either knob: each of them is busy with one
sound (a preview you can still cancel, the sound you are naming). A knob you turn while a layer's own button is held
past the hold time is always that layer's, whatever else is held.

### The engine picker

Hold **EDIT**. The engine picker opens and stays (EDIT blinks). It is a preview: nothing is final until you confirm.

| Control | In the engine picker |
| --- | --- |
| SELECT | the seven engines, one at a time, in the order of the table above (FM6 ... GRAIN). The sound moves to that engine and keeps its envelope and mix |
| KNOB 1 (or PRESETS) | the engine's pool, as PRESETS |
| KNOB 2 | INIT: the engine's plain starting sound (`init sound`) |
| the root keys and the chord keys | play, as always: you hear every engine and preset as you turn to it |
| OCT+, EDIT tap or EDIT held again | keep what you chose |
| OCT- or HOME | cancel: the sound as it was before |

Held from the view, EDIT picks for the chord sound. For the **bass engine**, open the bass sound's editor first (BASS
held + EDIT) and hold EDIT there.

### Saving

Tap **SAVE**. (With BASS held: the bass sound. In the editor: the sound being edited.)

1. Choose **Overwrite** or **Save as new** with SELECT: a turn is one step, and the list stops at its ends. (The
   three entries after them, **Favorite**, **Favorites filter** and **Favorite type**, are new in 0.22 and save
   nothing: [Favorites](#favorites), below. Until 0.22 KNOB 1 also moved through this list; it now changes the
   selected entry's value, as in the Options, and does nothing on the two saves.) The current name is under it, with `*` if you have changed the sound. The dialog opens on Overwrite when the sound is one of yours or has been changed, otherwise on
   Save as new.
   - **Overwrite** saves over the current preset at once and keeps its name. Over a factory preset this is allowed:
     your version takes its place in the pool and the factory one can be brought back. `00 INIT` cannot be
     overwritten; there Overwrite does a Save as new.
   - **Save as new** adds the sound at the end of the engine's pool and asks for a name.
2. **OCT+** or SAVE again takes the choice. **OCT-** or HOME cancels.
3. Naming (Save as new only), up to 12 characters:

   | Key | Types |
   | --- | --- |
   | the white roots D4 to G5 | `ABC` `DEF` `GHI` `JKL` `MNO` `PQRS` `TUV` `WXYZ` `0123` `4567` `89-.`; tap the same key again within 0.8 s for the next character of its group |
   | D#4 | a space |
   | F#4 | deletes the last character |
   | SELECT | moves the cursor, the red bar under the character you are editing. Turned right on the last character it adds a new one (`A`), up to 12 |
   | KNOB 1 | changes the character under the cursor through the whole set (space, A to Z, 0 to 9, `-`, `.`) |
   | KNOB 4 to the left | backspace: deletes the character under the cursor; the cursor moves to the one before it |
   | OCT+ or SAVE | saves (`saved FM6 26`) |
   | OCT- or HOME | cancels |

   The name starts as the current one, in grey, with the cursor on its last character; the first key you type
   replaces it, while SELECT, KNOB 1 and KNOB 4 edit it. A key types just after the cursor and the cursor moves onto
   the new character, so with the cursor on the last character it adds to the end, and in the middle of the name it
   inserts there; F#4 deletes the character under the cursor, as KNOB 4 does. So a name can be written with the
   knobs alone: KNOB 1 for a letter, SELECT one step right for the next (it starts as `A`), KNOB 1 again. While you
   are naming, KNOB 2, KNOB 3, PRESETS and ALGORITHM do nothing (new in 0.22: until then they kept their usual
   jobs, KNOB 2 changed the last character, and a turn of PRESETS changed the sound you were naming).

#### Favorites

New in 0.22. You can mark the sounds you like, and then have PRESETS and ALGORITHM step through the marked sounds
only. It all lives in the save menu (tap SAVE), which works like the Options: **SELECT picks an entry, KNOB 1 changes
its value.** Nothing here saves or overwrites a sound.

| Entry | The screen reads | KNOB 1 |
| --- | --- | --- |
| Overwrite, Save as new | the two ways to save, above | nothing |
| **Favorite** | `Favorite` or `Not a favorite` | right marks the sound, left takes the mark off |
| **Favorites filter** | `Filter on` or `Filter off` | right turns it on, left off |
| **Favorite type** | `Both`, `Chord` or `Bass` | steps through the three |

Three more knobs do the same job whichever entry is selected, so you rarely need to move:

| Knob | In the save menu |
| --- | --- |
| **KNOB 2** | the favorites filter: right on, left off |
| **KNOB 3** | the sound's type: Both, Chord, Bass |
| **KNOB 4** | the mark: right makes the sound a favorite, left takes the mark off |

A yellow note (`Filter on`, `Type: Chord`) confirms a change that the selected entry does not show. The footer of
the three favorites entries reads `K1 change · K2 filter · K3 type · K4 favorite`. OCT+ saves on Overwrite and Save
as new only; on the other three it does nothing. OCT-, HOME or SAVE closes the menu. While you are typing a name
(Save as new) the knobs belong to the name (step 3, above).

- **The mark.** A marked sound has a small yellow star before its name wherever the name is shown: when you turn
  PRESETS or ALGORITHM, in the save menu and in the editor's sound browser. You can also mark while browsing: in the
  browser (PRESETS turned in the sound editor) KNOB 4 marks the sound you are hearing, and the browser stays open.
  Any factory preset and any sound of yours can be a favorite; `00 INIT` cannot. A sound you have changed and not
  saved marks the preset it came from.
- **The type** says which part a favorite is for: `Chord` sounds are offered to the chord part only, `Bass` sounds to
  the bass part only, `Both` to both. Every sound is `Both` until you change it. The type belongs to the sound, not
  to the mark: take the mark off and put it back, and the type is still what you set. Changing the type of a sound
  that is not a favorite also marks it (so sorting sounds is one knob); KNOB 4 takes the mark off again without
  touching the type.
- **The filter.** With `Filter on`, PRESETS steps through the chord part's favorites of its engine only (typed Both
  or Chord), and ALGORITHM through the bass part's (Both or Bass), in the order of the pool; so does the BASS
  layer's Sound knob and the editor's browser. It is per engine: on FM6 you get FM6's favorites, on FM TONE those of
  FM TONE. `00 INIT` is skipped. ALGORITHM still passes through `off` between the last favorite and the first.
  - The big number is still the sound's place in the whole pool. The line under the stripes, in yellow, tells you the
    filter is on and where you are among the favorites: `favorites 03/07`.
  - If the sound you are on is not a favorite (you just turned the filter on, or took its mark off), it keeps
    playing; the next turn goes to the nearest favorite in that direction.
  - If the engine has no favorite for that part, the knob steps through all of its sounds as usual and the yellow
    line reads `no favorites: all`.
  - Changing the engine (GLO + PRESETS, GLO + ALGORITHM) lands on that engine's first favorite.
  - A MIDI program change, the Sound Manager and a loop's recorded sound changes are not filtered.

The marks, the types and the filter are kept by themselves, a moment after the FM-1 goes quiet (no SAVE held
needed), and they are part of a backup. Deleting one of your sounds removes its mark and its type.

To **delete** a sound you added, or to **reset** a factory preset you saved over: open the save dialog on it and hold
SAVE for a second. The screen asks `delete?` or `reset to factory?`; OCT+ does it, OCT- keeps it.

There is room for **128 saved sounds** in all (32 until 0.21), numbered U001 to U128, shared by the engines and the
two parts (`no free slot` when they are used up). A saved sound holds the engine's whole patch, its envelopes and modulation. (It also holds four send values, which are no longer used: the sends are the part's. An FM TONE sound's Drive is the one that is used: [FM TONE's Drive](#fm-tones-drive).)
Changes you have not saved are lost when you load another sound. A **global save** (SAVE held) keeps each part's
sound as it is, edited or not, without making it a preset: the FM-1 comes back with it after a power-off, and GLO +
SAVE brings it back (new in 0.18). The pool still holds the sound as it was saved as a preset, so turning PRESETS
away and back drops the edit, as before.

Level and Pan (the editor's MIX group) are the exception: they belong to the part, not to the sound. They stay as
they are when you change sound, and a global save keeps them.

### Sounds from outside

- An FM6 sound can receive a DX7 voice, and a CZ-1 sound a Casio tone, as SysEx over USB MIDI; save it afterwards.
  See [FM6.md](FM6.md) and [CZ1.md](CZ1.md).
- The installer page and the device manager move single sounds to and from files ([MIDI and USB](#15-midi-and-usb),
  [SOUNDS.md](SOUNDS.md)).
- A sound saved on an older version with the retired ANALOG engine loads on the VA
  (`ANALOG retired: on the VA`).
- A sound saved on PHASE, LOFI, VOICE, TRIO, NOISE or PHYS (retired in 0.18) stays in its slot under its name. You find it
  at the end of the VA's pool. It does not sound as it did: it plays the VA's plain starting sound with its own
  attack and release, and the screen says so (`PHASE retired: stand-in`). Nothing is deleted; the
  device manager and a backup still hold the sound as it was saved. If the chord or the bass was on one of these
  engines when you update, it comes up on FM6 `TINE EP` or VA `DEEP SUB`, as on a new unit.

More detail: [PRESETS.md](PRESETS.md).

## 12. The sound editor

The editor is the one dense place in ChoralRoot. It takes the whole screen, with no footer.

### In and out

| Gesture | Result |
| --- | --- |
| **EDIT** tap | the editor on the chord sound (EDIT blinks) |
| **BASS** held + **EDIT** | the editor on the bass sound. BASS has to be down; with the BASS layer merely open, EDIT opens the chord sound |
| **OPT** held + **EDIT**, in the editor | switch between the chord sound and the bass sound |
| **EDIT** tap or **HOME**, in the editor | leave |

While the editor is open the keys play as usual (chord block, roots, OCT- / OCT+, PANIC), MASTER is the volume,
ALGORITHM is the bass sound, and PERF works as outside. KEY, FX, BASS, LATCH, SEQ, LOOP and REC are not available:
their buttons are the editor's groups.

### Groups, screens, lanes

<img src="manual/editor-osc.png" width="240" alt="The editor on the VA sound LUSH PAD: the four oscillators as rows, OSC 1 active, columns Wave, Level, Coarse, Fine"> <img src="manual/editor-filter.png" width="240" alt="The editor's FILTER screen: the filter curve over two rows of four cells">

- Seven buttons are the **groups**. The top row follows the signal: OSC, FILT, ENV, LFO. The bottom row is the output
  stage: MOD, FX, MIX. The current group's button is lit.

  | Printed | FX | SEL | ENV | LFO | SEQ | PLAY | REC | GLO |
  | --- | --- | --- | --- | --- | --- | --- | --- | --- |
  | In the editor | **OSC** | **FILT** | **ENV** | **LFO** | **MOD** | **FX** | **MIX** | **SHIFT** |

- A group has one or more **screens**. Tapping the group's button again goes to its next screen.
- A screen has one or more **lanes**: the four values that are on KNOB 1 to KNOB 4 right now. The active lane is
  drawn in the knob colours; the rest is grey.
- **SELECT** steps through the lanes, and on past the last lane into the next screen, so SELECT alone reaches
  everything in a group.
- **KNOB 1 to KNOB 4** change the active lane's four values. Turned slowly a click is the finest step (one unit, or
  0.01 of a value with two decimals); turned quickly the steps grow, up to about 5 % of the range a click, so a quick
  spin crosses the whole range. A list (a wave, a mode, an algorithm, a ratio) is always one item a click.
- **SHIFT** (printed GLO, OPT outside) **snaps**: while it is held each click goes to the next common value
  ([Knob speed and OPT snap](#knob-speed-and-opt-snap)). Tap it to latch it (`snap` in the top line, the button lit).

### Knob speed and OPT snap

Every knob that changes a value (the sound editor of every engine, the FX layer, the BASS layer's level, the PERF
knobs, KNOB 3 and KNOB 4 on the main screen, Options > Velocity) follows how fast you turn it: slowly, the finest
step a click; quickly, bigger steps, up to about 5 % of the range a click. Lists (presets, engines, waves, modes,
algorithms, ratios, divisions) are always one item a click.

Hold **OPT** (GLO) while turning a value and it snaps, as the Digitone does with FUNC held:

| the value | snaps to |
|---|---|
| values with two decimals (FM TONE's Harmonics, Detune, Frequency, Resonance, Base, Level, Release, the offsets ...) | whole numbers |
| an LFO speed that goes both ways (FM TONE's) | −64 −32 −16 −8, 8 16 32 64 |
| transposes and coarse tunings in semitones | octaves |
| 0 to 127 | 0 16 32 48 64 80 96 112 127 |
| −64 to 63 (pan, amounts) | 0, then steps of 8 |
| 0 to 99 (FM6, CZ-1) | tens |
| a small range (up to 17 values) | one step a click |

Lists do not snap. OPT's other jobs do not change: OPT + PRESETS or ALGORITHM changes an engine, OPT + KNOB 1 the
split point, OPT + SELECT the click level.
- The top line has the sound's name (with `*` once changed, and `· BASS` in orange on the bass sound) and, at the
  right, where you are (`OSC 2 · A`, `ENV 2 · filter`, `MOD 3`).
- **PRESETS** in the editor opens the engine picker as a preview ([Sounds](#11-sounds)): the sound browser. KNOB 1
  or PRESETS steps the sounds, KNOB 2 is INIT, and since 0.22 **KNOB 4 marks the sound you hear as a favorite**
  (right: on, left: off; [Saving](#saving)).
- **SAVE** opens the save dialog for the sound being edited and returns to the editor.
- A layer opened over the editor (PERF held, EDIT held, HOME held) closes back to the editor. SAVE held is the
  global save there too.
- The editor remembers the group you were in and each group's screen and lane, until you switch off.

Two groups are the same for every engine:

| Group | Lanes | KNOB 1 to KNOB 4 |
| --- | --- | --- |
| **FX** | one | Drive · Chorus · Delay · Reverb: the part's four sends (its drive, and how much of it goes into the chorus, the delay and the reverb). They stay when the sound changes (on FM TONE the Drive is the sound's: [FM TONE's Drive](#fm-tones-drive)) |
| **MIX** | two | Level · Pan · Voice · Glide, and Trans · Detune · Priority · Gl. mode |

The sections below list what the other groups hold for each engine. They name the screens; for what each parameter
does, follow the link.

### VA

| Group | Screens |
| --- | --- |
| OSC | (1) the four oscillators as rows: Wave · Level · Coarse · Fine. (2) the same four: Mode · Shape · Key trk · Sync/Ring. (3) the mixer: the four levels as tall bars |
| FILT | the filter curve over two lanes: Cutoff · Reso · Ftype · Env amt, and Key trk · Spread · Drive |
| ENV | four screens, `ENV 1 · amp`, `ENV 2 · filter`, `ENV 3 · free`, `ENV 4 · free`, each the envelope's shape over Attack · Decay · Sustain · Release and Hold (ENV 1 also Velocity) |
| LFO | (1) the four LFOs as rows: Rate · Wave · Depth · Fade. (2) their Sync. (3) `VOICE`: Uspread, the stereo spread of a unison |
| MOD | the modulation matrix: eight rows of Source · Dest · Amount. The Amount goes from -64 to +63; below 6 either way it moves in tenths (a slow turn: +0.1, +0.2 ..), from 6 in whole steps, so a gentle vibrato is a small number: LFO to PITCH at +0.5 is about 9 cents with the LFO at full depth, +1 about 19 |

**Quick modulation** (the VA, and GRAIN from 0.24-beta2): hold **ENV** or **LFO** and turn a knob. The envelope or LFO you last looked at
(ENV 2 or LFO 1 if none yet) now modulates the parameter under that knob, and the turn sets the amount; the cell
shows `LFO1 +20`. A modulated parameter carries a small mark in its cell (yellow for an envelope, red for an LFO,
white for several sources). **OCT- held + a knob** removes every modulation of that parameter (`cleared`). The
matrix has eight slots (`matrix full`); a parameter that cannot be modulated says `not modulatable`.

More: [VA.md](VA.md).

### FM6

| Group | Screens |
| --- | --- |
| OSC | the six operators as rows, three screens: (1) Level · Coarse · Fine · Detune. (2) Mode · Vel sens · AM sens · Rate scl. (3) `SCALE`: Break pt · L depth · R depth · Curve |
| FILT | `ALGO`: Algo · Feedback · Transp · Osc sync |
| ENV | seven screens: `ENV 1` to `ENV 6`, one per operator, each the DX7 envelope's shape over Rate 1 to 4 and Level 1 to 4; then `PITCH EG` |
| LFO | `LFO`: Speed · Delay · PM depth · AM depth, and Wave · Sync · PM sens |
| MOD | (1) `FUNC`: Bend · Porta · Engine, and Step · Porta md · Gliss · DX vel. (2) `CTRL`: the wheel, foot, breath and aftertouch ranges and targets |

More: [FM6.md](FM6.md).

### FM TONE

| Group | Screens |
| --- | --- |
| OSC | (1) `SYN 1`, under a drawing of the algorithm: Algo · Ratio C · Ratio A · Ratio B, and Harm · Dtune · Feedback · Mix. (2) `SYN 2`: Offset C · Offset A · Offset B1 · Offset B2 |
| FILT | (1) `FILTER`, under the filter curve: Attack · Decay · Sustain · Release (the filter's envelope), and Freq · Reso · Type · Env depth. (2) `FILTER 2`: Env delay · Key track, and Base · Width |
| ENV | (1) `ENV A/B`, the two operator envelopes: Attack · Decay · End · Level for A and for B. (2) `ENV 2`: Delay · Trig · Reset for A and B, Phase, Velocity. (3) `ENV 3`: A Key · B1 Key · B2 Key. (4) `AMP`: Attack · Decay · Sustain · Release, and Level · Pan · Drive (0 to 127, saved with the sound, heard with FX off too) |
| LFO | three screens, `LFO 1` to `LFO 3`: Speed · Mult · Fade · Dest, and Wave · Phase · Trig · Depth |
| MOD | four fixed routes with an Amount each: ENV to FILT, ENV to PITCH, ENV to SHAPE, LFO to AMP |

Ratio B is two values in one cell: a click steps the upper one (B2), a click with SHIFT the lower one (B1); the top
line then reads `snap · B1`. More: [QUAD.md](QUAD.md) (the engine's working name was QUAD).

### CZ-1

| Group | Screens |
| --- | --- |
| OSC | (1) the two lines as rows: Wave · Wave · Window · Level. (2) `DETUNE`: Sign · Oct · Note · Fine. (3) the mixer: the two lines' levels |
| FILT | `DCW`: the key follow and velocity of the DCW (timbre) and of the DCA (volume), for each line |
| ENV | eighteen screens: for each of `PITCH 1`, `DCW 1`, `DCA 1`, `PITCH 2`, `DCW 2`, `DCA 2` the eight-step envelope on three screens: Rate and Level 1 to 4, Rate and Level 5 to 8, then Sustain and End |
| LFO | `VIB`: Wave · Delay · Rate · Depth |
| MOD | `TONE`: Line · Mod · Oct |

More: [CZ1.md](CZ1.md).

### P5

P5's pages are rows of up to four controls, like every engine's, each drawn as the Prophet-5's own: a knob with its
printed scale, a button with its LED (and the wave it switches drawn over it), the filter's REV with two LEDs, a lever
for the filter's keyboard tracking. The row on the knobs is in the knob colours, the others grey; SELECT moves from row
to row and on to the next screen, KNOB 1 to 4 turn the row's four places from left to right. A place with nothing in it
does nothing. The filter and the two envelopes each have a screen of their own, their curve on top and one big row
under it.

| Group | Screens: the rows, KNOB 1 to 4 (– an empty place) |
| --- | --- |
| OSC | (1) `OSC A`: FREQ · SAW · PULSE · P WIDTH; SYNC · MIX A · MIX B · NOISE (the mixer). (2) `OSC B`: FREQ · SAW · TRI · PULSE; FINE · P WIDTH · LO FREQ · KEYB |
| FILT | (1) `FILTER`, under the filter's curve: CUTOFF · RES · ENV AMT · REV (1/2 or 3: two LEDs). (2) `ENV 2 · filter`, under its envelope: ATTACK · DECAY · SUSTAIN · RELEASE. (3) `ENV 2 +`: VELOC · – · KEY TRK (Off, Half, Full: a lever) · AFTER |
| ENV | (1) `ENV 1 · amp`, the amplifier, under its envelope: ATTACK · DECAY · SUSTAIN · RELEASE. (2) `ENV 1 +`: VELOC · REL SW |
| LFO | (1) `LFO`: FREQ · SAW · TRI · SQUARE; INIT AMT · AFTER. (2) `WHEEL`, the wheel's modulation: SRC MIX (the LFO's noise mix) · FREQ A · FREQ B · PW A; PW B · FILTER |
| MOD | (1) `POLYMOD`: FILT ENV · OSC B (the amounts) · FREQ A · PW A; FILTER (where they go). (2) `VOICE`: UNISON · VOICES (1 to 4) · DETUNE · GLIDE; PRIORITY · VINTAGE. (3) `EXPR`, as on every engine: velocity and aftertouch, each Source · Dest · Amount |

Each cell is a value of the Prophet-5 program itself. A program whose Unison is on loads in UNISON (one note on up to
four stacked voices); the next one without it plays chords again. With Dual Core on the chord part has up to eight P5
voices, and a P5 bass up to four with the chord first (it always keeps one); with one core, four, three while a P5
bass sounds. More: [PROPHET.md](PROPHET.md).

### WHEEL

WHEEL has one screen in each group:

| Group | Holds |
| --- | --- |
| OSC | the engine's first four parameters (the screen carries the engine's own title, such as `BARS`) |
| FILT | its second four (again under the engine's own title) |
| ENV | Attack · Decay · Sustain · Release, under the envelope's shape |
| LFO | Rate · Wave · Vibrato · Wah |
| MOD | four fixed routes with an Amount each: ENV to FILT, ENV to PITCH, ENV to SHAPE, LFO to AMP |

### GRAIN

New in 0.24-beta2. GRAIN plays clouds of short grains read from two sources, **A** and **B**, that it makes itself:
each is about three quarters of a second of sound, made from four knobs whenever the sound loads or one of those
knobs turns. The new sound is written over the old one as you turn, a little each screen frame, while the grains go on
playing: nothing goes quiet, and turning a knob steadily moves the sound several times a second (from 0.25: until
0.24 a source fell silent while its knobs turned). A GRAIN sound stores its knobs, never audio. **MORPH** mixes the
two: every grain reads A and B at the same place and blends them, 0 A alone, 127 B alone, in between the two sounds
mixed into one, which the grains then play; MORPH moves smoothly under the matrix, ENV 2 and the LFOs (until 0.24 each
grain read one source or the other, MORPH setting how many read B).

| Group | Screens |
| --- | --- |
| OSC | (1) `SRC`, two lanes under the wave band: `SRC 1` (source A) and `SRC 2` (source B), each TYPE · TONE · MOTION · SEED. (2) `GRAIN`, two lanes under the wave band: SIZE · DENS · POS · SPRD, and (`GRAIN+`) MORPH · RAND · PITCH · SCAN |
| FILT | the VA's filter under its curve: (1) `FILTER`: CUT · RES · FTYPE · DRIVE. (2) `FILTER+`: KTRK |
| ENV | (1) `ENV 1 · amp`, the volume: ATK · DEC · SUS · REL. (2) `ENV 2 · mod`, an envelope for the matrix: ATK · DEC · SUS · REL. Each under the envelope's shape |
| LFO | (1) `LFO 1` and `LFO 2` as rows: RATE · WAVE · DEPTH · FADE. (2) `LFO SYN`: SYNC1 · SYNC2 |
| MOD | `MOD 1` to `MOD 8`, the matrix's eight slots: SRC · DST · AMT; then `EXPR`, as on every engine |

- **The source knobs.** **TYPE** picks one of eight families: PLUCK (a plucked string), DRONE (detuned saws under a
  moving filter), BELL (two-operator FM), BREATH (filtered noise), CHIRP (sweeping sines), SWEEP (a driven saw through
  a sweeping filter), VOWEL (a voice moving round A E I O U) and METAL (inharmonic partials with a strike). **TONE** is
  the brightness or colour within the family, **MOTION** how the material moves over its length (decay, sweep,
  vibrato, strums), **SEED** a variant: the same SEED always makes the same material.
- **The grain knobs.** **SIZE** is a grain's length (about 10 ms to 0.4 s), **DENS** how many grains start each second
  (1 to about 100), **POS** where in the material they start, **SPRD** how far round POS they scatter. **RAND** detunes
  the grains at random and plays some of them in reverse; **PITCH** transposes, -24 to +24 semitones. **SCAN** moves
  POS through the material by itself: right of 0 forward, left of 0 in reverse, very slowly near 0 (a pass in about
  two minutes) and faster further out (about half a second at the ends); 0 holds POS still.
- **The filter** is the VA's: **CUT**, **RES**, **FTYPE** (low-pass, band-pass, high-pass and notch, with the blends
  in between), **DRIVE** before it, and **KTRK**, how far the cutoff follows the note.
- **The LFOs** have the VA's waves; **FADE** brings an LFO in after the note starts. With **SYNC** on, RATE steps
  through the VA's tempo divisions.
- **The matrix.** Each slot sends a **SRC** (ENV1, ENV2, LFO1, LFO2, VEL, KEY, RAND, MODW: the mod wheel, CC 1 over
  MIDI) to a **DST** (MORPH, POS, SIZE, DENS, SPRD, RAND, PITCH, SCAN, CUT, RES, FTYPE, DRIVE, RATE1, RATE2, DEP1,
  DEP2, AMP) by **AMT**, -64 to +63, which moves in tenths below 6 either way and in whole steps from 6, as the VA's.
  Quick modulation works as on the VA: hold ENV or LFO and turn a knob.
- **The wave band.** On the SRC screen it draws the active lane's source, and redraws when a turn of its knobs has
  made it anew. On the GRAIN screen it draws the mix the grains read, its colour going from A's blue to B's orange as
  MORPH (as the last note plays it, modulation included) goes from A to B; POS is a white line (SCAN moves it), SPRD a
  green span round it, and every sounding grain a bright white line, travelling as it reads.
- **Chord and bass share the sources.** The FM-1 holds one pair: the chord sound's when it is on GRAIN (else the bass
  sound's). With both on GRAIN the bass plays the chord sound's A and B through its own grain knobs, MORPH, filter
  and envelopes.
- Notes are pitched from the material up to C6; above C6 they stop rising. The material is pitched at C4, a PLUCK
  6 cents above it; the grains read at source A's pitch, so a PLUCK as B plays 6 cents sharp of A (and a PLUCK as A
  leaves B 6 cents flat).

The factory presets: CLOUD PAD, SHIMMER, BREATH PAD, FROZEN, STRUM, SWARM, ACID DUST, GLITCH. More:
[GRAIN.md](GRAIN.md).

The whole editor: [EDITOR.md](EDITOR.md).

## 13. Views

The view is what the screen shows when no layer, menu or editor is open. There are six.

| View | Shows |
| --- | --- |
| **Chord** (default) | the chord name large, the voiced notes under it; while an arpeggio or a pattern plays, the notes on a line with the sounding one hopping along ([Performance modes](#7-performance-modes)) |
| **Keyboard** | the chord name and a keyboard with the chord's notes lit |
| **Notes** | the chord name small and the notes as a list |
| **Geek Out** | the chord, its notes and keyboard, and status lines: voicing and play style, bass and tempo, the audio load, the second core (`c1 off`, `c1 ok ...`), the transposition |
| **Scope** | the waveform of the output, with the chord name small above it |
| **Geek Out 2** (new in 0.22) | four meters: the audio load of core 0 and of core 1 (the bass part, its share of a Prophet chord's voices and the reverb), the RAM and the POOL in use; the peak of the last second as a tick. With one core running, core 1's meter is grey and says why (`off`, `failed`, `gave up`) |

- **HOME** tap goes back to the view from anywhere. On the view itself it steps to the next view.
- **HOME held** for 0.6 s opens the view menu: SELECT or the white keys D4 to B4 choose the view, KNOB 1 (or C5)
  switches **Lock** on and off; OCT+, OCT-, HOME or holding HOME again closes it. With Lock on, a HOME tap never changes the view; it only goes home.
- Options > View sets the same thing.

Options > Motion (Full, Calm, Off) sets how much the screens animate; the hopping note of an arpeggio is not an animation and shows with Off too. The sound editor never animates. The other five views never change with the performance mode.

## 14. Options

Tap **OPT** (printed GLO). One setting fills the screen:

<img src="manual/options.png" width="240" alt="An Options page: Play Style, value Simple, the next entry Extension Addition under it, the battery at the top right">

- **SELECT** moves to the next or previous setting.
- **KNOB 1** changes the value. Changes act at once.
- **OPT**, **HOME**, **OCT-** or **OCT+** closes the Options. The bottom line of the screen says so.
- **The keys play**, exactly as on the view you opened the Options from: chords, LOCK, Key Mode, the split, the
  arpeggio, LATCH, and a loop being recorded takes what you play. Their lights show what plays, and OPT is lit while
  the Options are open. So you can hold a chord and turn KNOB 1 to hear a setting change: Output between Classic and
  Clean, for one. KNOB 2 to 4, PRESETS and ALGORITHM also keep the jobs they have on the view. Only OCT- and OCT+ are
  different: they close the Options instead of shifting the octave. (Until 0.19 the white keys jumped to the first
  eleven settings and nothing sounded.)
- No key confirms or cancels anything in the Options. Calibrate and Reset Settings are started with OCT+ only.

| Setting | Values | Default | What it does |
| --- | --- | --- | --- |
| **Play Style** | Simple, Advanced, Free | Simple | what chord keys do while a root is held ([Play styles](#play-styles)) |
| **Extension Addition** | Add Note, Play Chord | Add Note | in Advanced and Free: an extension adds its note, or replays the chord |
| **Secret Chords** | Off, Simple, All | Off | two chord types together make a third chord ([Secret chords](#secret-chords)) |
| **Chord Keys** | FM-1, Classic | FM-1 (since 0.25.1; an updated unit is moved to it once) | which keys are the chord keys: FM-1 (SUS = G#3 + A#3, the 9 on B3, C4 and C#4 roots, LOCK on LFO; [The FM-1 chord keys](#the-fm-1-chord-keys)) or Classic, the Orchid's (DIM MIN MAJ SUS on F#3 G#3 A#3 C#4, the 9 on C4, LOCK on B3; [The Classic chord keys](#the-classic-chord-keys)). Not on the SMK-37 |
| **SUS Window** | 30 to 80 ms, 5 ms a step | 30 ms | listed with the FM-1 chord keys only: how long a root waits for the second key of G#3 + A#3 ([The FM-1 chord keys](#the-fm-1-chord-keys)) |
| **Full K Shorts** | Off, On | Off | on the Full keyboard, F#5 brings the chord keys back and G5 plays a chord on every key ([Key Mode](#key-mode)) |
| **MIDI Keys** | Off, ch 1 to ch 16 | Off | the channel whose notes 53 to 79 press the FM-1's keys F3 to G5 ([MIDI in](#midi-in-usb-and-the-trs-jack)); the MIDI Perform and MIDI Bass channels are skipped |
| **6th with 7th** | In place, Octave up | In place | where the 6th sounds in a chord that also has a seventh (7 or M7). In place: 9 semitones above the root, under the seventh, as the Orchid plays it. Octave up: an octave higher, on top, which makes it a 13th chord (D + 7 + 6: D F# A C B). Chords without a seventh never change; the name does not change ([Types and extensions](#types-and-extensions)) |
| **Velocity** | 1 to 127 | 100 | the velocity of every key, for the sound and for MIDI out |
| **Bass Behaviour** | Chords Only, Unison Bass, Bass Single Notes, Solo | Chords Only | what the bass plays ([Bass](#6-bass)) |
| **Single Notes** | Full Octave, Split Oct, Split Bass | Full Octave | Split Oct: single notes below the split point sound an octave lower. Split Bass: the keys below it play the bass part ([Key Mode](#key-mode)) |
| **Split Point** | C to B | F | where that split is on the Chords keyboard; reads `(Split off)` while Single Notes is Full Octave. The Full keyboard splits at its second F |
| **MIDI Perform** | Off, ch 1 to ch 16 | ch 1 | the channel the chord sound's notes go out on, and come in on |
| **MIDI Bass** | Off, ch 1 to ch 16 | ch 2 | the same for the bass |
| **MIDI Raw Chord** | Off, ch 1 to ch 16 | Off | the channel for the plain chord, without the performance mode. From Off the first step is ch 1; choose a channel of its own (the Orchid uses 3) |
| **Raw Chord Sound** | Off, On | Off | On: the plain chord also plays on the chord sound, under an arpeggio or pattern. With no performance mode on, every note then sounds twice |
| **MIDI Clock** | Off, Out, In | Out | Out: sends clock, and start / stop with the loop. In: follows them |
| **View** | Chord, Keyboard, Notes, Geek Out, Scope, Geek Out 2 | Chord | the view ([Views](#13-views)) |
| **Motion** | Full, Calm, Off | Full | screen animation |
| **Chord Names** | Orchid, Standard | Orchid | how a chord is named on every screen. Orchid: by the keys held, as the Orchid does (`A` `7 9`, `JAZZ`). Standard: as a lead sheet names the notes that sound (`A9`, `Aadd9`, `A13`). Only the name changes, never the notes ([Chord names](#chord-names)) |
| **LEDs** | Glow, Stock | Glow | Glow: idle keys glow dim, active ones are bright. Stock: the reverse |
| **Hold Time** | 300, 400, 500, 600 ms | 400 ms | how long a hold is |
| **USB Record** | Off, On | On | On: the computer can record the FM-1 ([USB audio recording](#usb-audio-recording)) |
| **USB Level** | Master, Fixed | Master | Master: the recording follows the MASTER knob. Fixed: it is recorded at full level and MASTER only sets what you hear |
| **USB Rate** | 44.1 kHz, 44.1 + 48 kHz | 44.1 kHz | 44.1 kHz: the computer records at the FM-1's own rate, as in 0.18. 44.1 + 48 kHz: the computer may also choose 48 kHz, which the FM-1 converts for it; that takes some of its processor time while a recording runs ([USB audio recording](#usb-audio-recording)). The FM-1 leaves USB for a second when you change it |
| **Dual Core** | Off, On | On (a new unit; an updated unit keeps what it had) | On: the chip's second core shares the work, so more voices sound (below). Takes effect after a restart |
| **Output** | Classic, Clean | Clean | the last stage before the headphones, the speaker and the USB recording. Classic: as 0.18 and before. Clean: 0.19's, with less distortion on chords and at low MASTER settings; a single note is a little quieter, a full chord with the bass a little louder, and everything sounds 0.7 ms later (its limiter looks ahead). You can switch while you play to compare |
| **Version** | | | the firmware's version; the line under it is this FM-1's unit ID (`unit ID A1B2C3D4`, its flash chip's: the same after any install), which tells several FM-1s on one computer apart (`fm1_install.py --unit`, the device manager) |
| **Calibrate** | | | OCT+ starts the panel calibration ([Calibration](#calibration)) |
| **Reset Settings** | | | OCT+ twice puts every setting back to its default (below) |

In SAFE MODE two more entries appear at the end, Safe Mode and Flash Data ([SAFE MODE](#safe-mode)).

**Reset Settings** is the last entry: turn SELECT to the end of the list. It puts the FM-1 back to the settings a
new one has, in one step, which is the quick way out when an option you do not remember setting makes it behave
strangely, and the way to make sure two people are looking at the same instrument. Press **OCT+**: the entry reads
`OCT+ again: reset`. Press **OCT+** again and it is done: `settings reset` shows, and you are back on the view.
Anything else cancels: OCT-, HOME, turning SELECT to another entry, closing the Options.

- **Reset:** everything in the Options (Play Style back to Simple, 6th with 7th back to In place, Motion, Chord Names, LEDs, Hold Time, the MIDI channels and
  clock, USB Record, USB Level and USB Rate, Dual Core and the rest), Key Mode with its tonic, scale and Transpose, Single
  Notes and the split point, the KEY layer's keys, Key plays and Keyboard, the LOCK layer's two settings, the
  performance mode with all its parameters and whether it is on, LATCH, the effects being on, the tempo, the
  metronome, the looper's options (length, record mode, quantize, count-in, level, what its keys do), the bass
  behaviour and register, the view and its lock, and which sound each part plays: the chord part is on TINE EP
  again, the bass on DEEP SUB, and every engine is back at its first preset.
- **Kept:** the sounds you saved and every patch and bank you loaded, the loops in their slots and the slot you were
  on, and the panel calibration. Reset Settings never touches those; SAFE MODE's Flash Data is the entry that erases
  data, and Calibrate the one that changes the panel table.
- **What happens to what you were doing:** it ends like a PANIC: every note stops, a playing loop stops (it stays in
  its slot; a take you were recording is dropped), LOCK and LATCH go off, the octave shift and the voicing return to
  0, the bass is off, the parts' Level and Pan and the effects' settings are back at their normal values, and a
  change to a sound you had not saved is gone. The FM-1 is then as a new unit is, and stays so over a power-off:
  the reset saves itself, no SAVE held is needed after it.
- **Dual Core** is the one setting that waits for a restart: the reset sets it On, and if it was Off the message
  reads `settings reset · restart`: the second core starts when you switch off and on. Right after the FM-1 turned
  Dual Core off by itself (below), the reset keeps it Off until you switch it On in Options.

**Dual Core.** With Dual Core On, the default of a new unit since 0.22, the chip's second core shares the work: the
bass part plays there with its own voices, so a full chord and the bass both sound whole. With it Off, ChoralRoot
runs on one core as the stock firmware does, and the chord and the bass share one budget of voices: a very full
chord with the bass can cost a voice. **Updating to 0.22 does not change your setting**: a unit that had Dual Core
Off (the default from 0.16 to 0.21) still has it Off. To use the second core, set Options > Dual Core to On and
switch off and on (or use Reset Settings). Firmwares that run both cores are reported to crash on some FM-1s;
ChoralRoot raises the core regulator to 1.50 V (0.22; the level the other FM-1 firmwares settled on) before the
second core starts and falls back to one core if the second misbehaves, but the margin varies from unit to unit.
**If the FM-1 crashes, hangs or gives the second core up while Dual Core is On, it turns the option Off by itself**
(0.22): after the restart the screen says `dual core off: crash` (or `hang`, `gave up`), the line under Options >
Dual Core says why it is Off, and it stays Off over power cycles until you turn it On again. If your FM-1 ever
crashes or resets while playing and Dual Core is still On, set it to Off and switch off and on: it is the first
thing to try. If the FM-1 restarts by itself right after you switch it on, it comes up on one core that time (Geek
Out: `c1 failed`), so that you can reach Options and set Dual Core to Off. The value reads `On · restart` or
`Off · restart` until you switch off and on. The Geek Out view shows the state: `c1 off`, `c1 ok ...`, `c1 failed`,
`c1 gave up · Options>Dual Core`, or `c1 off · after a crash`; Geek Out 2 shows each core's load as a meter. More:
[DUALCORE.md](DUALCORE.md).

**The single core package.** If your FM-1 crashes or freezes with the standard package, and setting Dual Core to
Off is not enough (or you cannot reach Options), install the **Single core** package: in the web installer open
**Advanced** under the versions and choose **Hardware fallback > Single core** (from 0.22). It is the same firmware,
built so that the second core is never started: every sound and feature is there, and with one core a very full
chord with the bass can cost a voice, as with Dual Core Off. Options > Dual Core then reads `1 core build` (it
cannot be changed there) and Options > Version reads `ChoralRoot 0.22 1-core`. Your settings, sounds, loops and
favorites stay when you move between the two packages, both ways; installing the standard package again (Install
with Standard chosen, the default) puts Dual Core back as your settings say.

### What the FM-1 remembers

**Saving is manual** (0.18). The FM-1 comes back, after switching off, as you last **saved** it, not as you left it.

- **Hold SAVE** past the hold time, with no other button: the **global save**. Everything about how the instrument is
  set up to play is stored as it is now, together with the loop in memory. The screen says `saved` (a green box).
  There is no question to answer.
- **Hold OPT (GLO) and SAVE together for a second**: **revert**. The FM-1 goes back to the last global save, as if
  you had switched it off and on: every note stops and the loop stops, as with PANIC; any layer, menu, dialog or
  editor closes; then the saved state is there again, the loop of the saved slot included. The screen says
  `reverted`. No question here either. Use it to try things and take them back: change a handful of settings, an
  edit, a loop; then either hold SAVE to keep them, or GLO + SAVE to be where you were.
- **A SAVE tap** is still the save-preset dialog ([Saving](#saving)). Saving a sound as a preset is stored at
  once, as before, and so is a loop when you move to another slot.
- **The Options are the exception: they are stored by themselves**, as every setting was until 0.17: a moment after
  the instrument falls quiet (about two seconds without a change and without a sound), never while a loop plays. So
  is the panel calibration. A revert does not touch the Options.

| A global save keeps (SAVE held) | Stored by itself | Never kept, on purpose |
| --- | --- | --- |
| Key Mode, the tonic, the scale, Transpose; **Single Notes and the split point**; the KEY layer's keys (pick or play), Key plays, Keyboard and Numerals | **Options:** Play Style, Extension Addition, Secret Chords, 6th with 7th, Velocity | notes that were sounding, a chord LATCH was holding |
| the performance mode, its parameters, and whether it is on | MIDI Perform, MIDI Bass, MIDI Raw Chord, Raw Chord Sound, MIDI Clock | a playing loop: its slot is loaded, **stopped**. It never starts by itself |
| LATCH; LOCK and what it latched; the LOCK layer's two settings | Motion, Chord Names, LEDs, Hold Time | a take you were recording |
| the voicing on KNOB 1 and the octave shift | USB Record, USB Level, USB Rate, Dual Core, Output | an open layer, menu, dialog or editor: the FM-1 starts on the view |
| the bass being on, its **behaviour**, register, level, and which sound it plays | the panel calibration | where you were in the editor, the KEY layer's and the PLAY menu's page |
| the effects in full: on or off, the page picked, every cell of the reverb, the chorus and the delay, both parts' sends | | |
| which sound each part plays and each engine's last place; **each part's sound as it is, with changes you have not saved as a preset**; the parts' Level and Pan | | |
| the tempo; the metronome, its time signature and level | | |
| the looper's settings (Length, **Rate**, Quantize, Count-in, Level, the record mode, what its keys do), the slot, and **the loop in memory** | | |
| the **view** and its lock | | |

Three settings can be reached both from the Options and from a layer: **Bass Behaviour** (the BASS layer), **Single
Notes** and **Split Point** (the KEY layer, OPT + KNOB 1), and **View** (HOME, the view menu). They go with the layer:
they are part of the global save, also when you set them in the Options. Everything else in the Options list is stored
by itself. Saved presets, loaded patches and banks, and the loops of the other slots are stored when you make them
and are not part of either column.

**What you hear when you save.** Writing to the FM-1's memory stops the sound for about 45 ms for each thing that is
written, and the global save writes at most three things: the settings, the state of the instrument (the sounds, the
effects, the voicing ...), and the loop. Only what changed is written; a save with nothing changed writes nothing.
In silence you hear nothing. Into a held chord or a ringing tail you hear one to three short gaps, about a twentieth
of a second each; the sound goes on after them. The FM-1 does not wait for silence, because you pressed and expect it
saved now. It never writes while a loop plays or a take is open: SAVE held then answers `saves at stop` and the save
happens when the loop stops. What the FM-1 stores by itself (the Options) still waits for silence, always.

- **A new FM-1**, or one that has never been saved, starts with the defaults.
- **Options > Reset Settings** resets both columns and stores the result at once: no SAVE held is needed after it.
- **An FM-1 updated from an earlier 0.18 build or from 0.17**: what it had stored is simply its last save.
- After a PANIC nothing is stored: a PANIC is not a save.
- The **update gesture** and a **backup** do not save for you either. Hold SAVE first if what you have set is to
  survive the update or be in the backup. A backup holds the settings, the saved sounds and the loops; it does not
  carry the state of the instrument (the voicing, the edited sounds, the effects' cells ...), and after a restore the
  FM-1 starts from the restored settings (the saved sounds, the bass off).
- In SAFE MODE nothing is read and nothing is saved (`safe mode: no saving`).
- Going back to 0.17 and returning: 0.17 does not know the state of the instrument and starts without it, and once it
  has stored a setting of its own, 0.18 starts from 0.17's settings rather than from an older save of its own.

More: [SETTINGS.md](SETTINGS.md).

## 15. MIDI and USB

Over USB the computer sees one device, **ChoralRoot FM-1**: a MIDI port of that name, and an audio input,
**ChoralRoot In**.

### MIDI out (USB)

| What | Channel (Options) | Carries |
| --- | --- | --- |
| Perform | MIDI Perform, ch 1 | what the chord sound plays: the chord, or the performance mode's notes |
| Bass | MIDI Bass, ch 2 | the bass line |
| Raw Chord | MIDI Raw Chord, Off | the plain chord as a block, whatever the performance mode does |

With MIDI Clock set to Out (the default) the FM-1 also sends MIDI clock at the tempo, a start when the loop starts
playing and a stop when it stops. PANIC sends all notes off (CC 123).

### MIDI in (USB and the TRS jack)

| Message | Effect |
| --- | --- |
| notes on the MIDI Perform channel | play the chord sound directly, as an ordinary synth (no chords are built) |
| notes on the MIDI Bass channel | play the bass sound |
| program change | the sound's place in its engine's pool (chord: 0 = INIT; bass: 0 = off, 1 = INIT, and so on) |
| CC 7 | the part's level |
| CC 91, 93, 94 | the part's reverb, chorus and delay sends (they stay until changed: a program change does not reset them). The screen shows the new send in per cent: controller value 127 is 100 % |
| CC 64, pitch bend, CC 1, aftertouch | sustain, bend, and the modulation sources of the engines that use them |
| clock, start, continue, stop | with MIDI Clock set to In: the tempo follows the clock and the loop follows start / continue / stop |

A part whose channel is Off ignores MIDI in.

**MIDI Keys** (Options > MIDI Keys, Off by default; new in 0.24): on that channel the notes 53 to 79 press the FM-1's
own keys F3 to G5, exactly as the keybed does: the chord keys of the layout you chose, the roots, LOCK, the SUS
Window, the Full keyboard and its shortcuts. The note's velocity plays the key; CC 120 and CC 123 let every key go;
other notes and messages on that channel do nothing. It cannot be the MIDI Perform or MIDI Bass channel (those are
skipped, and setting one of them to it switches MIDI Keys off).

### USB audio recording

**ChoralRoot In** is a class-compliant audio input: no driver is needed. It has six channels, 16 bit, at 44.1 kHz;
with Options > USB Rate set to `44.1 + 48 kHz` the computer can also run it at 48 kHz.

| Channels | Signal |
| --- | --- |
| 1-2 | Master: what you hear, without the metronome click |
| 3-4 | the chord sound alone, before the reverb, chorus and delay, at half level |
| 5-6 | the bass sound alone, the same way |

The three pairs line up to the sample. With Options > Output on Clean all three are 0.7 ms later than with Classic
(Clean's limiter looks ahead), together. Set Output before a take, not during one: the master pair fades over, but
the chord and bass pairs jump by those 0.7 ms at the switch.

- **44.1 kHz** is the FM-1's own rate: the recording is exactly what the FM-1 made. As the FM-1 comes, it is the
  only rate ChoralRoot In offers (Options > USB Rate: `44.1 kHz`), exactly as in 0.18.
- **48 kHz** (from 0.19) is for phones, tablets, apps and projects that only take 48 kHz. Set **Options > USB Rate**
  to `44.1 + 48 kHz`: the FM-1 reconnects (`USB RECONNECTING`, about a second), and the computer, phone or app
  can then choose either rate. At 48 kHz the FM-1 converts on the way out. The top of the treble is a little softer
  (1 dB down at 16 kHz), and the conversion takes about 6 % of the processor while a recording runs: in the very
  heaviest sounds (CZ-1 chords with both effects) a voice may drop sooner. That is why it is off until you ask for
  it: a Mac chooses 48 kHz by itself as soon as it is offered.
- With USB Rate at `44.1 + 48 kHz`, on a Mac the rate is ChoralRoot In's Format in Audio MIDI Setup; on Windows,
  Sound settings > ChoralRoot In > Properties > Advanced. A DAW usually sets it from the project's rate. Choose
  44 100 there when you do not need 48 kHz.
- Options > USB Level = Fixed records at full level while MASTER only turns the speaker and headphones down.
- Options > USB Record = Off removes the audio input. When you change it, or USB Rate, the FM-1 leaves USB for
  about a second (`USB RECONNECTING`); the MIDI port goes with it and comes back. It waits for a backup, a restore
  or an update that is under way, so do not be surprised if the message comes a moment later. Do not change either
  during a take: the computer sees the FM-1 unplugged and plugged in again.
- The computer cannot play audio through the FM-1.

More: [USB-AUDIO.md](USB-AUDIO.md).

### The web device manager

A page on the ChoralRoot site, next to the installer
(<https://quixotic7.github.io/ChoralRootFM1/webapp/installer/manager.html>, Chrome or Edge), that shows what is
stored on the FM-1 beside a library on the computer:

- the pool of every engine as the FM-1 has it, and the ten loop slots;
- a library: sounds kept in the browser, files, and the factory sounds as packs;
- Place (library to FM-1), Keep (FM-1 to library), Swap, Rename, Delete, drag to reorder, Undo;
- Restore the factory presets your sounds replaced (the selected, one engine's or every engine's), and Delete all user
  sounds / Reset sounds to factory: each asks first and offers a backup;
- selecting a row on the FM-1 side switches the FM-1 to that sound, so you hear it (this needs 0.16);
- an on-screen keyboard, and Back up everything / Install a backup.

It will not write while a loop plays ("Stop the loop on the FM-1 first"). Adding `?sim=1` to the address runs it
against a simulated FM-1. More: [DEVICE-MANAGER.md](DEVICE-MANAGER.md).

### Backup and restore

The installer page (<https://quixotic7.github.io/ChoralRootFM1/>) has **Back up** and **Restore**:

- A backup is one `.json` file with everything stored on the FM-1: the settings, the saved sounds with their
  patches, the ten loops, the FM6 voice bank and the CZ-1 banks.
- Restore checks the file, writes it back and restarts the FM-1. Stop any loop first.
- Install saves a backup before it writes, unless you skip it.
- The page's Sounds section exports, imports, renames and deletes single sounds as files (FM6 voices also as DX7
  `.syx`, CZ-1 tones as Casio dumps).

The same from the command line: `tools/fm1_install.py` (see the [README](../README.md) and [SOUNDS.md](SOUNDS.md)).

## 16. Updating and recovery

### Before every update

The FM-1 updates on its own battery, and the installer cannot read how full it is.

1. **Charge the FM-1 fully.**
2. Plug it **straight into the computer** with a cable that carries data. No hub.
3. Keep the computer awake.
4. Leave the cable alone until the installer says Done.

### Updating

Open the installer, <https://quixotic7.github.io/ChoralRootFM1/webapp/installer/> (the red button on the site), in
Chrome or Edge on a computer, connect the FM-1 and press **Install ChoralRoot 0.22**. That is all of it: the page
finds the FM-1, saves a backup first, switches the FM-1 to update mode, writes the firmware and restarts it. About
two minutes. Safari and Firefox cannot install (the page says so); Chrome on Android has what it takes but is not
tested.

Beside the button the page lists what is new in the version it installs, with a link to the full notes. The button
always names the version it writes. **What is on my FM-1?** under it reads what the FM-1 runs without writing
anything (ChoralRoot and its version, the stock firmware, Felucca, Melodee, or a firmware the page will not install
over), and after an install it reads the version back.

Your saved sounds, loops and settings are kept. What you have not saved is not: hold SAVE first.
Options > Version shows what is installed.

**Going back to an older version:** under the button, **Other versions** offers 0.21 down to 0.16. One click on
one of them shows its date, what it brought and what going back to it means, and the button now says, for example,
"Install ChoralRoot 0.21", with the going-back sentence above it. "Back to 0.22, the newest" undoes the choice. The
install is the same. Your saved sounds and loops stay; what the older version does not know it ignores or plays
without (0.21 has no P5 and no favorites and shows its own 32 sound slots; 0.18 and older have no
Options > Output; the page has it for each version, from the "Going back" parts of the release notes). Beta releases
are listed there too once you tick **Show beta releases** (the page remembers it in that browser); the button's
default stays the newest stable release. 0.15 and older are not offered: they have no protected update path, and
0.14 and 0.15 stop responding after ten idle minutes. They remain on GitHub.

Updating to 0.16 sets Options > Dual Core to Off. Updating to 0.22 leaves it as it is (a new unit has it On).

### If an update is interrupted

From 0.16 on, ChoralRoot checks at every start that the firmware was written completely. If an update was cut off
(the cable, the battery, the computer going to sleep):

1. Switch the FM-1 off and on again.
2. Some of the key lights flash **three short times** and the screen stays black. The FM-1 is in update mode.
3. Open the installer and press **Install** again. It finishes the write.

If the computer side merely stopped while the FM-1 stayed on, the FM-1 is still in update mode: replug the cable and
press Install again.

The update **to** 0.16 from an older version is the last one without this protection during its first second,
because the part that does the checking is itself being written then.

### Power-on and held gestures

| Gesture | Result |
| --- | --- |
| **OCT- alone, held while switching on** | update mode on purpose: three short flashes, black screen. Open the installer and Install, or switch off and on to start ChoralRoot again (nothing was written) |
| **OCT- and OCT+ held while switching on** | the panel calibration (below). Let go as soon as the screen says `let go` |
| **OCT-, OCT+ and HOME held for 5 s while ChoralRoot runs** | update mode without switching off (new in 0.18). The two OCT keys do their PANIC first; with all three down, after 2 s a red countdown `3 2 1` (`update mode: let go to cancel`: letting go of any of the three cancels, and nothing else happens), at 5 s three short flashes and a black screen: update mode, the same as OCT- at power-on. Open the installer and Install, or switch off and on to start ChoralRoot again (nothing was written). What you were doing is stored first and is back afterwards |
| **OCT- and OCT+ held, without HOME** | PANIC when they go down, and nothing more however long you hold them. (Until 0.17 this was the 5 s gesture, and it led to the chip's USB boot, which the installer cannot use; it was too easy to hit) |

Holding OCT- alone while switching on is new in 0.16. Hold it until the lights have flashed. Recovery after an
interrupted update does not depend on the key. The three-button gesture works in SAFE MODE too. If the countdown
says `USB boot: let go to cancel` instead, this FM-1's ChoralRoot was not installed by ChoralRoot's own installer
since 0.16 and has no update loader to hand over to: at 5 s it goes to the chip's USB boot (a black screen, a disk
named `WL82 UBOOT1.00`, of no use to the installer). Switch off and on, and install the usual way.

**One long flash instead of three** means the FM-1 went to the chip's USB boot (the `WL82 UBOOT1.00` disk) instead of
update mode. Switch it off for 10 seconds and try once more: a second OCT- start straight after a first one ends
there. If it is one long flash again, no update loader is stored on the unit; switch off and on to start ChoralRoot.

### Calibration

The calibration teaches ChoralRoot which physical button and knob is which. You only need it if buttons or knobs do
the wrong jobs, or a knob turns backwards.

1. Start it with Options > Calibrate, OCT+ (or hold OCT- and OCT+ while switching on).
2. The screen names each control in turn: press the 14 buttons one by one as it asks, then turn each of the seven
   knobs to the right. The names are the printed ones; the SEL button is called `SCL`.
3. At `done`, OCT+ keeps the result and OCT- throws it away. Thirty seconds without input cancels.

### SAFE MODE

If ChoralRoot crashes twice in a row within 30 seconds of starting (switching off and on does not count), it starts
in SAFE MODE: a yellow `SAFE MODE` screen, and `Safe mode` in red on the top line.

- It plays with the factory sounds and default settings. What is stored on the FM-1 (settings, saved sounds, loops)
  is ignored and left untouched, and nothing can be saved (`safe mode: no saving`).
- The installer and Back up still work, so you can save a backup.
- Switching off and on tries the stored data again.
- Options opens on two extra entries. **Safe Mode** is information. **Flash Data**, OCT+ twice, erases the stored
  settings, the saved sounds (all 128 user slots), the favourites and the loops (not the firmware) and restarts.
  (0.22 to 0.24-beta2 left the saved sounds in place.)
- If SAFE MODE itself crashes twice in a row, the FM-1 falls back to the chip's USB boot (the `WL82 UBOOT1.00`
  disk); switch off and on.

### If the FM-1 stays dark

A black screen with a `WL82 UBOOT1.00` disk on the computer, or nothing on USB at all, is beyond this manual: follow
"If the FM-1 no longer starts" in the [README](../README.md) and the checklist in
[INSTALL-COMPAT.md](INSTALL-COMPAT.md), which lead to
[MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker). Do not format the disk.

### Back to the stock firmware

The installer's "Return to official V15" section installs M-VAVE's own firmware, which you download yourself from
M-VAVE. It saves a backup first: the stock firmware cannot use ChoralRoot's sounds, loops or settings. To bring them
back, install ChoralRoot again and press Restore.

More: [BOOT-SAFETY.md](BOOT-SAFETY.md), [INSTALL-COMPAT.md](INSTALL-COMPAT.md), the
[0.16 release notes](releases/0.16.md).

## 17. Troubleshooting

| What you see or hear | Likely cause | What to do |
| --- | --- | --- |
| Keys or knobs do something unexpected | a layer or the editor is open (a button blinks), or the Options are (OPT is lit: SELECT, KNOB 1, OCT- and OCT+ are theirs; the keys play) | HOME |
| The FM-1 behaves strangely and you cannot tell which setting it is | some option is not at its default | Options > Reset Settings, OCT+ twice: every setting back to default; your sounds, loops and the calibration stay ([Options](#14-options)) |
| A layer will not go away | layers stay open by design | OCT- or HOME |
| Plain root keys play chords | Key Mode is on (KEY lit, `Key: C`) | tap KEY |
| Chords come out in the wrong key | Transpose is set (Geek Out shows `Trans`) | KEY layer, KNOB 3 back to 0 |
| Low single notes jump down an octave | Single Notes is Split Oct | KEY layer, KNOB 4: Full |
| Some roots play a bass note instead of a chord | Single Notes is Split Bass | KEY layer, KNOB 4: Full |
| The chord keys play notes | Keyboard is Full | KEY layer, SELECT to page 2, KNOB 2: Chords |
| Some keys do nothing | Key Mode is on with Key plays = Notes: they are outside the scale | tap KEY, or KEY layer page 2 (Key plays, or Snap) |
| Chord keys stay on | LOCK is on (LFO lit; Classic chord keys: B3 lit) | tap LFO (Classic: B3) |
| Notes keep sounding | LATCH is on (LFO pulsing), or Strum's Hold | GLO held + LFO (Classic: tap LFO); PANIC stops everything |
| SUS will not come, or C4 / C#4 play roots | the FM-1 chord keys (the default since 0.25.1): SUS is G#3 + A#3, the 9 is B3 | press G#3 and A#3 together, or Options > Chord Keys: Classic |
| The chord sound is silent, the bass plays | Bass Behaviour is Solo (`Bass Solo`) | BASS layer, another behaviour, or tap BASS |
| No bass | the bass is off (a new unit, a reset, or you switched it off) | tap BASS |
| The bass layer's keys make no sound | in that layer the white keys pick the behaviour | close the layer to play |
| The loop plays but is silent | its Level is 0 | PLAY menu, page 2, KNOB 1 |
| REC replaced my loop | the record mode is Overwrite | PLAY menu, ALGORITHM: Overdub. Before the first chord, REC again cancels |
| In step entry two notes land on the same step | they were held together | let go of the first before pressing the next; OCT- goes back |
| Step entry says `64 steps: full` | a pattern has 64 steps at most | REC to finish; OCT- to go back and change a step |
| HOME does not change the view | the view is locked | HOME held, KNOB 1: Lock Off |
| The tempo moves by itself | MIDI Clock is In | Options > MIDI Clock |
| My reverb or delay setting is back to what it was | it was not saved: since 0.18 the FM-1 comes back as last **saved** | set it again and hold SAVE (`saved`) |
| The reverb does not stop | Decay is at `Inf`, or very long | turn Decay down (FX layer, Reverb 1, KNOB 3) |
| Every sound is dull, or thin, whatever I load | the master filter is on (0.22) | FX layer, Filter: KNOB 4 to `Off`, or the cuts to `Off` |
| The echoes are on one side, then the other | the delay's Mode is Ping-pong | FX layer, Delay 2, KNOB 2: Stereo |
| The bass has reverb or echo | the bass part has sends of its own | BASS held + EDIT, the FX group; or FX off |
| A sound I saved with lots of reverb loads dry (or the other way round) | sends are no longer loaded with a sound: they are the part's | set the sends once in the FX layer (its first page); they stay for every sound |
| An FM TONE sound is distorted although FX is off, or the drive changes when I turn PRESETS | on FM TONE the Drive is the sound's: saved with it, loaded with it, not switched off by FX | the sound editor, ENV group, AMP screen, Drive (or the Sends page's Drive); SAVE to keep it |
| In the engine picker the white keys no longer choose the engine | they always play now, so that you hear what you turn to | SELECT steps the engines |
| After updating to 0.18 the reverb and the delay sound different | they are new, and start at their defaults | [FX](#8-fx) |
| My edit is gone | unsaved changes are lost with the next sound, and at power-off unless you held SAVE | SAVE tap to make it a preset; SAVE held to keep the part as it is |
| `no free slot` | all 128 saved sounds are used | delete one (SAVE held in the save dialog), or use the device manager |
| A setting did not survive switching off | it was not saved: only the Options are stored by themselves (a moment after the instrument falls quiet, never while a loop plays) | hold SAVE before switching off |
| My loop is gone after switching off | loops are no longer saved when they stop | hold SAVE; moving to another slot also saves the loop you leave |
| I recorded over a loop I liked | | GLO + SAVE held for a second: the loop as last saved is back (with everything else as last saved) |
| SAVE held no longer shows the loop slots | it is the global save now | PLAY menu: PRESETS picks the slot; clear + save empties one |
| `saves at stop` | SAVE was held while a loop played or a take was open: nothing is written then | stop the loop: `saved` |
| The bass came on by itself at power-on | it was on at your last save (since 0.18 it comes back) | tap BASS, hold SAVE |
| Every note sounds doubled and louder | Raw Chord Sound is On with no performance mode | Options > Raw Chord Sound: Off |
| The screen shows the logo, or is black | three minutes, or ten, without input | touch anything |
| `Safe mode` on the top line | SAFE MODE | see [SAFE MODE](#safe-mode) |
| The computer shows no audio input | USB Record is Off | Options > USB Record: On |
| The FM-1 drops off USB for a second | USB Record or USB Rate was changed (`USB RECONNECTING`) | wait |
| The computer offers 44.1 kHz only for ChoralRoot In | USB Rate is `44.1 kHz` (the default) | Options > USB Rate: `44.1 + 48 kHz` |
| Voices drop while the computer records, not otherwise | the computer records at 48 kHz, which the FM-1 converts | choose 44 100 Hz on the computer, or Options > USB Rate: `44.1 kHz` |
| The FM-1 became a `WL82 UBOOT1.00` disk | no update loader is stored on it and update mode was asked for (OCT- at power-on, or OCT- + OCT+ + HOME for 5 s) | switch off and on |
| The screen went black with three flashes while I played | OCT-, OCT+ and HOME were held for 5 s: update mode | switch off and on; nothing was written |
| The FM-1 crashes or resets while playing | with Dual Core On, possibly the second core | since 0.22 the FM-1 turns Dual Core Off by itself after a crash (`dual core off: crash` after the restart); on 0.21 and before: Options > Dual Core: Off, switch off and on. Please report it either way |
| `dual core off: crash` (or `hang`, `gave up`) after a restart | the FM-1 crashed, hung or lost the second core while Dual Core was On: this unit cannot run both cores at this speed | nothing to do: it runs on one core now, as the default; Options > Dual Core shows why it is Off. Please tell us (`fm1_install.py --debug`) |
| Buttons or knobs are mixed up | the panel table is wrong | [Calibration](#calibration) |

## 18. Quick reference

### Buttons

A held layer button opens its layer and it stays; holding the same button again closes it (so do OCT- and HOME).

| Button (printed label) | ChoralRoot | Tap | Hold |
| --- | --- | --- | --- |
| FX | FX | effects on / off (in its layer too) | FX layer: SELECT the page (Sends · Reverb 1 · Reverb 2 · Chorus · Delay 1 · Delay 2 · Drive) · KNOB 1-4 its cells (Sends: the chord part's drive, chorus, delay and reverb sends; KNOB 4 on an effect's first page: its Send) |
| SEL | KEY | Key Mode on / off | KEY layer: a root = tonic · MAJ / MIN = scale · KNOB 1 Tonic · 2 Scale · 3 Transpose · 4 Single (Full / Split Oct / Split Bass) · SELECT: page 2: KNOB 1 Key plays (Chords / Notes) · 2 Keyboard (Chords / Full) · 4 Numerals (Off / On / Solfège) · GLO: the keys pick / play |
| ENV | BASS | bass on / off | BASS layer: SELECT behaviour · KNOB 1 Behaviour · 2 Register · 3 Sound · 4 Level. Held + EDIT: edit the bass sound. Held + SAVE: save it |
| LFO | LOCK (Classic chord keys: LATCH) | LOCK on / off; GLO held + LFO: LATCH (Classic: latch on / off) | the LOCK layer: KNOB 1 Type key Clears / Keeps · KNOB 2 Ext keys Toggle / Pushed (Classic: nothing) |
| EDIT | EDIT | the sound editor; in it: leave | the engine picker: SELECT engine · KNOB 1 presets · KNOB 2 init · the keys play · OCT+ keep · OCT- cancel |
| GLO | OPT | Options (SELECT setting · KNOB 1 value). In the PLAY menu: its keys play / loops. In the KEY layer: its keys pick / play. In the editor: SHIFT latch | + **SAVE, both held 1 s: revert to the last save** · + PRESETS: engine · + ALGORITHM: bass engine (both wherever the knob alone is the sound: the main screen, the Options, the layers but PLAY; ALGORITHM in the sound editor too) · + KNOB 1: split point · + SELECT: click level · + SEQ: metronome on / off · + REC: the next record mode. In the editor: snap (held or latched); + EDIT: chord / bass sound. Held + a value knob anywhere: the value snaps |
| HOME | HOME | back; on the view: next view | 0.6 s: the view menu (SELECT view · KNOB 1 lock) |
| SAVE | SAVE | the save menu: SELECT Overwrite / Save as new / Favorite / Favorites filter / Favorite type · KNOB 1 value · KNOB 2 filter · 3 type · 4 favorite · OCT+ saves · held 1 s: delete / reset | **the global save**: everything as it is now, and the loop (`saved`). With GLO, both held 1 s: revert (`reverted`) |
| ARP | PERF | performance on / off (in its layer too) | PERF layer: SELECT mode · KNOB 1-4 its parameters |
| SEQ | SEQ | the loop editor; in it: the PLAY menu; in the PLAY menu: leave. With GLO held: metronome on / off | its hold menu (metronome): SELECT time signature · KNOB 1 Click · a tap there: metronome on / off |
| PLAY | LOOP | play / stop the loop; ends a take | PLAY menu: PRESETS slot · ALGORITHM record mode · SELECT page · page 1: KNOB 1 Length · 2 Rate · 3 Quantize · 4 Count-in · page 2: KNOB 1 Level · 2 Metronome · GLO: its keys play / loops · SEQ: leave · EDIT: the loop editor (PRESETS step, heard while the loop is stopped, with GLO 8 steps · SELECT page · ALGORITHM the step's voicing · the keys replace it · REC clears it · OCT the octave · page 1: KNOB 1 Note len · 2 Steps, with GLO to 8 16 32 48 64 · 3 Chance · 4 Velocity · page 2: KNOB 1 Ratchet · 2 Condition · 3 Voicing · SEQ, EDIT or LOOP held: the PLAY menu · HOME: the view) |
| REC | REC | record by the record mode; ends a take; cancels when armed. With GLO held: the next record mode | undo the last layer |
| OCT- | octave down | roots down an octave (to -2). In layers, menus, dialogs: back / cancel (closes). In step entry: back a step | in the editor, + a knob: clear that parameter's modulation |
| OCT+ | octave up | roots up an octave (to +2). In layers, menus, dialogs: confirm (keep, save, do it); where there is nothing to confirm it closes, as OCT-. In step entry: a rest | |
| OCT- + OCT+ | PANIC | all notes off | nothing more; with HOME as well, 5 s: update mode (a countdown from 2 s, let go to cancel) |

### In the sound editor

| Button (printed label) | In the editor | Tap | Hold |
| --- | --- | --- | --- |
| FX | OSC | the group; again: its next screen | |
| SEL | FILT | the same | |
| ENV | ENV | the same | + a knob: that envelope modulates the knob's parameter (VA) |
| LFO | LFO | the same | + a knob: that LFO modulates it (VA) |
| SEQ | MOD | the same | |
| PLAY | FX | the group | |
| REC | MIX | the group | |
| GLO | SHIFT | latch snap | snap while held; + EDIT: chord / bass sound |
| EDIT | EDIT | leave | the engine picker |
| HOME | HOME | leave | the view menu |
| SAVE | SAVE | the save dialog | the global save, as outside |
| ARP | PERF | performance on / off | the PERF layer |

SELECT: the next lane. KNOB 1 to KNOB 4: the lane's values. PRESETS: the engine picker.

### Knobs

| Knob | Turn | OPT + turn | In a layer | In the editor |
| --- | --- | --- | --- | --- |
| MASTER | volume | | volume | volume |
| SELECT | tempo | click level | the layer's picker (KEY layer: its page; LOCK layer: tempo) | the lane |
| PRESETS | chord sound | the chord sound's engine, wherever PRESETS alone is the chord sound: the main screen, the Options, the layers (PLAY menu: the slot, as without OPT; loop editor: 8 steps; the editor, its picker and the save dialog: as without OPT) | chord sound (PLAY menu: slot; loop editor: the step) | the engine picker |
| ALGORITHM | bass sound (with BASS held: bass level) | the bass sound's engine, wherever ALGORITHM alone is the bass sound: the main screen, the Options, the layers, the sound editor (PLAY menu: the record mode, as without OPT; loop editor: the step's Voicing, `Follow` / `0`; the editor's picker and the save dialog: as without OPT) | bass sound (PLAY menu: record mode; loop editor: the step's Voicing; with BASS held: the BASS layer's Level) | bass sound |
| KNOB 1 | voicing (Full keyboard: the octave) | split point | the layer's first cell | the lane's first value |
| KNOB 2 | bass register | | second cell | second value |
| KNOB 3 | performance parameter | | third cell | third value |
| KNOB 4 | the chord part's send into the selected effect | | fourth cell | fourth value |

### Keys

| Key | Job |
| --- | --- |
| F#3 G#3 A#3 | DIM MIN MAJ; G#3 + A#3 together: SUS (Classic chord keys: SUS on C#4) |
| F3 G3 A3 B3 | 6 m7 M7 9 (Classic: the 9 on C4, B3 is LOCK: tap on / off, held the LOCK layer) |
| LFO (a button) | LOCK (above). With LOCK on, the only latched type pressed again alone: off (single notes, the extensions kept) |
| F3 to G5, Keyboard = Full | 27 single notes (no chord keys; LOCK on LFO, none in Classic); the split at F4. Key Mode on with Key plays Chords: the key's chords. Full K Shorts On: F3 to F5 notes, F#5 the chord keys, G5 chords on every key |
| C4 to G5 | roots (Classic: D4 to G5) |
| white roots D4 to F5, PLAY menu with its keys on loops (GLO) | loop slots 1 to 10 |
| D#4, F#4, PLAY menu with its keys on loops | clear the loop (hold 1 s), undo |
| white roots, naming a sound | letters and digits; D#4 a space; F#4 delete |
| white roots D4 to B4, then C5, view menu | Chord, Keyboard, Notes, Geek Out, Scope, Geek Out 2; Lock |
| white roots D4 to G4, BASS layer | the bass behaviour |
| white roots D4 to F4, the SEQ key's hold menu (metronome) | 4/4, 3/4, 6/8 |

### At power-on

| Held while switching on | Result |
| --- | --- |
| OCT- | update mode (three flashes); new in 0.16 |
| OCT- + OCT+ | the panel calibration |

## 19. Where to read more

| Subject | Document |
| --- | --- |
| The sound editor in full | [EDITOR.md](EDITOR.md) |
| The engines | [VA.md](VA.md), [FM6.md](FM6.md), [QUAD.md](QUAD.md) (FM TONE), [CZ1.md](CZ1.md), [PROPHET.md](PROPHET.md) (P5), [GRAIN.md](GRAIN.md) |
| Presets and saved sounds | [PRESETS.md](PRESETS.md), [SOUNDS.md](SOUNDS.md) |
| The looper | [LOOPER.md](LOOPER.md), [LOOPER-MODES.md](LOOPER-MODES.md) |
| What is stored, and how | [SETTINGS.md](SETTINGS.md) |
| The second core | [DUALCORE.md](DUALCORE.md) |
| USB audio | [USB-AUDIO.md](USB-AUDIO.md) |
| The delay and the reverb: how they are built, what they cost | [FX.md](FX.md) |
| The device manager | [DEVICE-MANAGER.md](DEVICE-MANAGER.md) |
| Updating, recovery, which firmware to install over | [BOOT-SAFETY.md](BOOT-SAFETY.md), [INSTALL-COMPAT.md](INSTALL-COMPAT.md) |
| What changed in each version | [releases/](releases/) |
| The design, and the emulator | [PLAN.md](../PLAN.md), [tools/emu/README.md](../tools/emu/README.md) |

Where one of these documents and this manual disagree about how the instrument behaves, the instrument is right;
please report it.

The screenshots in this manual are from the emulator, which runs the firmware's own drawing code:
[manual/shots.txt](manual/shots.txt) is the script that makes them.
