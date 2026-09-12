# Adjacent Reality — User Manual

The Adjacent Reality hardware runs the AdjacentReality firmware, which is based heavily on Captain Credible's Fantasy Mansion firmware.

This documentation is written from the firmware in `AdjacentReality/` and matched to the official Adjacent Reality OPERATION MANUAL. Every control described here maps to a real code path; references are `file:line`.

---

## 1. What you have and how power works

The Adjacent Reality is a battery-powered generative EP or "sentient instrument." It composes
melodies, bass lines and drum patterns, has a light sensor and two buttons, and can clock-sync with
other gear (PO-32, Korg, Minijam .hub).

**Power is controlled by a slide switch** that selects between two CR2032 coin cells. Flip the
switch to turn the unit on or off (and to select which battery to use).

There is also a **RESET button** (`SW-RESET1`). Pressing RESET restarts the firmware exactly like
powering off and on again — you can use it in place of "power off and on" for any boot-mode
instruction below.

**Battery:** two CR2032 coin cells. Replace them by turning the unit off and opening the holder. Switch between the two batteries using the slide switch.

---

## 2. The controls

| Control | What it is | Role |
|---|---|---|
| **MATTER** (SW1) | tactile switch on PB0 | Live trigger / edit / boot-mode selector |
| **ANTIMATTER** (SW2) | tactile switch on PB3 | Live trigger / edit / boot-mode selector |
| **Light sensor** (LDR, PB2) | CdS photocell | Context-sensitive light control — effect depends on which button is held |
| **LED** (PB1) | yellow-green LED | Beat / sync indicator; also lights the LDR at boot to seed randomness |
| **Jack** | 3.5 mm stereo | TIP (LEFT) = drums & sync; RING (RIGHT) = melody |
| **Ambient Light Calibration** (RV1) | 10 kΩ trimmer pot | One-time setup — adjusts LDR range for your room's lighting |

The buttons are momentary and are used like drum pads or step-sequencer edit buttons — tap, hold, or
release. The light sensor is the small mostly-round device with the zig-zag pattern on top. You move
your hand over it while pressing/holding one of the buttons.

---

## 3. Boot modes (choose a mode at power-on)

The unit reads the buttons the instant it powers up, so **hold the button(s) down *before* turning
the unit on (or press RESET) and keep them held for a second after the unit starts.** Which buttons
are held selects the sync/audio layout for the whole session:

### Mode A — Normal / Autotune (no buttons held)
Do not press either button. Turn the unit on (or press RESET).
Melody plays out the **RIGHT** channel (ring); drums out the LEFT (tip). **No sync clock is sent** —
use MATTER or ANTIMATTER if you need to clock other gear.

### Mode B — MATTER (hold MATTER only)
**To enter:** press and hold MATTER, then turn the unit on (or press RESET). Keep
holding for about a second after it starts, then release.
Melody plays out the RIGHT channel; **sync is transmitted out the LEFT** channel (tip). **The drum
generator is disabled** (drums are muted for this whole session).

### Mode C — ANTIMATTER (hold ANTIMATTER only)
**To enter:** press and hold ANTIMATTER, then turn the unit on (or press RESET). Keep
holding for about a second, then release.
This is the mirror image of MATTER: **drums play out the LEFT channel** (tip), and **sync is
transmitted out the RIGHT channel** (ring). Melody is switched off for the whole session.

### Mode D — Slave / sync follower (hold BOTH buttons)
**To enter:** press and hold BOTH buttons, then turn the unit on (or press RESET). The unit
goes into "listening" — keep holding both buttons until it locks onto an external clock, then release
both.

In this mode the unit **does not generate its own tempo**. It waits for a clock signal on either
channel and auto-selects its role:

- A rising clock pulse on the **LEFT (tip)** → the unit plays **melody** out the RIGHT channel,
  following the incoming clock.
- A rising clock pulse on the **RIGHT (ring)** → the unit plays **drums** out the LEFT channel and
  jumps straight into **My First Beat** mode (see §5, mode 4).

The clock must be **sustained** for it to be accepted — brief electrical noise on an unplugged cable
won't trigger a role. If **no clock arrives within about 5 seconds**, the unit stops listening and
boots as a normal **Autotune** master instead (just release both buttons and play on).

**Cable convention:** sync = TIP (LEFT), audio = RING (RIGHT) — standard for .hub / PO-32.

The Adjacent Reality always acts as the **master** clock when it is the one sending sync (MATTER or
ANTIMATTER boots). Its ~3 V sync is strong enough to drive a PO-32. In slave mode it follows an
external clock, but a PO-32's sync *output* is too weak (~1 V) to drive the Adjacent Reality's ~1.8 V input

threshold — so in practice the Adjacent Reality leads and other gear follows.
The **LED** blinks on every beat/clock tick, so it doubles as a tempo check even without speakers.

---

## 4. During playback: the universal "next mode" gesture

At any time while the music is playing, you can advance to the next mode with both buttons:

1. **Press and hold both buttons together.** You'll hear a pitch (each mode has a unique
   note — clustered modes share a chromatic range, standalone modes sit a fourth apart) and
   the music collapses into a noise drone while you keep holding.
   The LED blinks N times where N is the next mode number — a quick visual confirmation.
2. **Release both buttons together.** The unit steps to the next mode, re-rolls a new
   random composition, and starts the new mode.

Each press-and-release advances exactly one mode. To step through several
modes, repeat the full press-and-release gesture that many times. **The pitch fires on the press;
the advance happens on the release**, so you must fully press and release each time — holding both
down and pulsing them will not advance the mode.

The mode pitches and blink counts (entering mode N plays this note and blinks N times):

| Mode | Name | Pitch | Freq | Blinks |
|---|---|---|---|---|
| 0 | **Autotune** | C6 | 1048 Hz | 1 |
| 1 | **Autotune** | A#5 | 932 Hz | 2 |
| 2 | **My First Melody** | C#6 | 1108 Hz | 3 |
| 3 | **My First Bassline** | D6 | 1176 Hz | 4 |
| 4 | **My First Beat** | D#6 | 1240 Hz | 5 |
| 5 | **Pimp My Shit** | E6 | 1320 Hz | 6 |
| 6 | **Mutant Theremin** | A6 | 1760 Hz | 7 |
| 7 | **Portal Glitch** | F5 | 698 Hz | 8 |

Modes 0, 2, 3, 4, 5 form a tight chromatic cluster (C6–E6). Modes 1, 6, 7 are spaced apart by fourths — mode 1 sits a fourth below (A#5), mode 6 a fourth above (A6), and mode 7 two fourths below (F5). The cue tone is pre-compensated for synth state, so it always sounds at its correct pitch regardless of the current mode's octave or pitch bend settings.

The mode order (starting from a normal boot) is:

| Advance # | Mode | What it is |
|---|---|---|
| start | **1** | **Autotune** (generative song + live performance) |
| 1 | **2** | **My First Melody** (record notes with the light) |
| 2 | **3** | **My First Bassline** (record bass notes with the light) |
| 3 | **4** | **My First Beat** (build a drum pattern with the light) |
| 4 | **5** | **Pimp My Shit** (do shit to your composition — same as Autotune) |
| 5 | **6** | **Mutant Theremin** (generative noise/texture performance) |
| 6 | **7** | **Portal Glitch** (light-pitched notes over a beat) |
| 7 | **0** | **Autotune** (wraps around; the cycle continues) |

> **Your recordings survive mode changes.** Once you write a melody or beat, the unit marks it as
> preserved and keeps playing it when you come back to Autotune mode — only the random *background*
> parameters are re-rolled as you step through modes.

---

## 5. Controls in each mode

The buttons trigger or edit; the light sensor (LDR) modifies the sound, but **only when a button is
held.** The LDR is not a free-floating continuous controller — its effect is gated by the buttons
and varies by mode. Cover the sensor with your hand while holding a button to change the sound.

### Mode 1 / 5 / 0 — Autotune (generative song)
Fetches melody, bassline, chords and drums from a parallel universe and lets you perform over them.

- **Hold MATTER** — pitch-bend is active. Bright light bends the melody pitch up; covering
  the sensor bends it down. A chaotic drum-noise hit also fires. The bend range is moderate — you'll
  hear a subtle sweep on high notes and a wider sweep on low notes. The octave also shifts at certain
  light levels, which can cause a sudden jump overlaid on the bend.
- **Hold ANTIMATTER** — the light sensor controls either octave or note decay, depending on the
  song's randomly-rolled "mood" (you won't know which until you try). Octave mode gives you about 5
  discrete steps, not a smooth sweep. Decay mode changes how long notes ring out, but only affects
  notes triggered after you move your hand — notes already playing keep their old decay.
- **Release MATTER** — a fresh random drum pattern rolls in, and **drums toggle on/off**
  (one release = drums off, next release = drums on).
- **Release ANTIMATTER** — cycles what you hear: Melody **+ Bass** → Melody **only** → Bass
  **only** → repeat. Each release steps to the next combination.

### Mode 2 — My First Melody (record notes with the light)
Records your own melody, step by step, using the light to pick pitch. Drums and bass keep playing as
a reference track.

- **Hold ANTIMATTER** — as each sequencer step passes, the note you're pointing at with the
  light is written into the melody. **Bright light writes a high note; covering the sensor writes a
  low note.** Very dark light also flags the note to play an octave higher.
- **Hold MATTER** — erases all notes on the step currently passing.

### Mode 3 — My First Bassline (record bass notes with the light)
The same recorder, but for the bass line.

- **Hold ANTIMATTER** — writes a bass note (picked by the light) at the current step.
- **Hold MATTER** — erases the bass note at the current step.

### Mode 4 — My First Beat (make your own sick beat)
Builds your own 16-step drum pattern. The drums are silenced until you write something.

- **Hold ANTIMATTER** — places a drum hit on the **next** step. The light chooses the drum:
  - **Dark (covered):** kick (BD)
  - **Mid light:** hi-hat (HH)
  - **Bright (uncovered):** snare (SD)
- **Hold MATTER** — clears the drum hit on the next step (all three types).

### Mode 6 — Mutant Theremin (generative noise/texture)
A hands-on noise/solo playground. Drums are muted.

- **Hold ANTIMATTER** — plays a generative noise/texture whose character changes as you move
  your hand over the light sensor.
- **Hold MATTER** — enables pitch-bend of the tone.
- **Release both** — the machine re-rolls its generative parameters and picks a random octave.

### Mode 7 — Portal Glitch
Generative drum patterns plus light-pitched notes.

- **Press ANTIMATTER** — steps through 5 different drum/timing patterns (each press picks the
  next one).
- **Hold MATTER** — plays a note whose pitch follows the light sensor (with distortion),
  and continuously re-rolls the random generators.
- **Release both** — let it play the current pattern on its own.

---

## 6. Light sensor (LDR) and Ambient Light Calibration

### How the LDR works

The sensor is read with an internal pull-up: **brighter light → lower reading; covering it → higher
reading.** The processed value (`x`) ranges from 0 (dark) to about 624 (very bright).

**Important:** The LDR does not continuously control the music on its own. Its effect is **gated by
the buttons** — you must be holding a button for the LDR to do anything in most modes. The one
exception is My First Beat, where the LDR selects the drum type while you write. Outside of these
contexts, the LDR reads a value but does not route it to the sound engine.

### What the LDR actually controls

| Mode | Button | LDR effect | What to expect |
|---|---|---|---|
| Autotune | MATTER held | Pitch bend | Subtle sweep; wider on low notes, narrower on high notes. Octave shifts can jump. |
| Autotune | ANTIMATTER held | Octave or decay | 5 discrete octave steps OR decay change (only affects new notes). You can't tell which without trying. |
| My First Melody | ANTIMATTER held | Selects note pitch | Bright = high note, covered = low note. Written at each sequencer step. |
| My First Bassline | ANTIMATTER held | Selects bass note | Same as My First Melody, for bass. |
| My First Beat | ANTIMATTER held | Selects drum type | Dark = kick, mid = hi-hat, bright = snare. Three buckets only. |
| Mutant Theremin | ANTIMATTER held | Waveform character | Changes the noise texture as you move your hand. |
| Portal Glitch | MATTER held | Note pitch | Plays a note whose pitch follows the light. |

### Ambient Light Calibration (trimmer pot)

The Adjacent Reality board includes a trimmer potentiometer labeled **"Ambient Light Calibration"** in series with
the LDR. This is a **one-time setup adjustment**, not a performance control.

**When you need it:** In very bright rooms (direct sunlight, strong studio lighting), the LDR
resistance drops so low that the sensor saturates — covering it barely changes the reading. The
trimmer adds resistance to shift the range back into a usable window.

**How to set it:** While the unit is playing in its normal room, turn the trimmer until covering the
sensor with your hand produces a noticeable change in the sound (try holding MATTER in Autotune mode
and sweeping your hand). Once set, you should not need to adjust it again unless you move the unit
to a significantly different lighting environment.

**When you don't need it:** In dim or moderately lit rooms, the LDR already has a usable range
without the trimmer. Setting it to the midpoint (5kΩ) is a safe default.

### Tips

- The LDR responds slowly (~20–30 ms). Sweep your hand slowly for the sequencer to track it.
- The "sweet spot" depends entirely on your room's lighting. There is no universal setting.
- In Autotune mode, try holding MATTER and slowly moving your hand from fully covered to fully
  uncovered. The pitch bend is the most audible LDR effect.
- If the LDR seems unresponsive, try the Ambient Light Calibration trimmer or move to a
  dimmer/brighter spot.

---

## 7. Quick reference card

```
POWER ON            Flip slide switch / press RESET
POWER OFF           Flip slide switch
BATTERIES           Two CR2032 cells — slide switch selects between them

BOOT MODES          (hold before power-on or RESET, release ~1s after it starts)
  none held         Autotune — melody RIGHT, drums LEFT, no sync sent
  MATTER held       MATTER — melody RIGHT, sync LEFT, drums OFF
  ANTIMATTER held   ANTIMATTER — drums LEFT, sync RIGHT, melody OFF
  BOTH held         Slave — waits for external clock; LEFT pulse = melody,
                    RIGHT pulse = drums (My First Beat)

NEXT MODE           Press+hold BOTH buttons (hear pitch + drone + LED blinks), release both
                    = advance one mode. Each mode has a unique pitch:
                    C6=Autotune A#5=Autotune C#6=My First Melody D6=My First Bassline
                    D#6=My First Beat E6=Pimp My Shit A6=Mutant Theremin F5=Portal Glitch
                    (modes 0,2,3,4,5 clustered C6–E6; modes 1,6,7 spaced by fourths)
                    (pitch=on press, LED blinks N times for mode N, advance=on release)

AUTOTUNE (1/5/0)
  hold MATTER       pitch-bend w/ light (subtle) |  release MATTER   new drums + drums on/off
  hold ANTIMATTER   light = octave steps or decay |  release ANTIMATTER  both / melody / bass cycle

MY FIRST MELODY (2) hold ANTIMATTER = write light-picked note @ step, hold MATTER = erase step
MY FIRST BASS (3)   hold ANTIMATTER = write light-picked bass note, hold MATTER = erase step
MY FIRST BEAT (4)   hold ANTIMATTER = place drum @ next step (dark=kick,mid=hat,bright=snare)
                    hold MATTER = clear next step
MUTANT THEREMIN (6) hold ANTIMATTER = noise texture, hold MATTER = bend, release = re-roll
PORTAL GLITCH (7)   press ANTIMATTER = next pattern, hold MATTER = light-pitched note

AMBIENT LIGHT       Turn trimmer until covering sensor produces audible change
CALIBRATION         (one-time setup, not a performance control)
```
