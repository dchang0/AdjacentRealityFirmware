# Adjacent Reality Firmware Changelog

## 1.0.3

### Bug fixes

- **Sync state leaking across mode transitions**: `handleModeTransition()` now clears `receivingSync`, `sendingSync`, resets `syncOutputPin` to 0, and resets `watchdogcounter` to 0. Without this, a slave-mode sync lock would persist into the next mode, preventing the WDT ISR from advancing steps, and accumulated ticks could cause an unwanted immediate step
- **Cue tone voice collision guard**: `playSequencerStep()` now checks `!cueToneActive` before calling `processStep()`, preventing the sequencer from overwriting the cue tone voice while it decays (~100ms)
- **WDT interrupt re-enable in triggerNote**: Added `WDTCR |= 1 << WDIE` to re-enable the watchdog timer interrupt if it was cleared, preventing the sequencer from stalling

### Features

- **Arduino-free build**: Created `arduino.h` — a complete minimal Arduino-core replacement (pinMode, digitalWrite, analogRead, random, etc.) via direct PORTB/ADCB register access. Eliminates the Arduino core / ATTinyCore dependency entirely
- **Dedicated pin definitions**: New `pins.h` with `BTN_1`, `BTN_2`, `TONE_PIN`, `DRUM_PIN`, `LDR_ADC_CHANNEL`, `LDR_PIN`
- **CLI build script**: `build.sh` for building with avr-g++ directly
- **Microchip Studio project**: `AdjacentReality.cppproj` with ATtiny85 target, -Os optimization, hex/lss/eep output
- **Mode transition drum pattern rotation**: `drumPatternIndex` now rotates through 4 drum sample algorithms on each mode change
- **Mode transition noteDecay reset**: `noteDecay` reset to 6 before playing cue tone, ensuring consistent envelope duration
- **OSCCAL oscillator calibration nudge**: `OSCCAL += 2` at boot nudges the ATtiny85's internal RC oscillator for better tuning
- **Slave boot TONE_PIN mode**: When slave detects sync on TONE_PIN, mode set to 4 (Record Beat) to capture incoming drum patterns

### Refactoring

- **Redundant flag assignment removal**: Removed duplicate `allowKeyTransposition = true`, `tonesEnabled = true` from `setup()` and `allowSnareChange = true`, `allowKickChange = true` from `modeRecordMelody` that re-set values already initialized by struct definitions
- **freqBase extraction**: Refactored duplicated `freqSelector` calculation in `processStep()` — common `freqBase` variable computed once per iteration, eliminating duplicated modulation calculation

### Dead code cleanup

- **`NOTE_TRIGGER_DISTORTION` unused**: `#define NOTE_TRIGGER_DISTORTION 10` defined but never referenced — remnant from earlier cue tone amplitude design
- **Stale motif pattern comments**: config.h retains extensive ASCII-art rhythm documentation for the removed motif system with no indication they are historical

### Documentation

- **Architecture overview**: AdjacentReality.cpp header now documents the two ISR drivers (Timer0 at 20kHz, WDT at ~62Hz), main loop responsibilities, and KEY DATA FLOW diagram
- **Comprehensive per-function docs**: All `.cpp` files now have multi-paragraph block comments explaining algorithms, data structures, and design rationale
- **New documentation files**: `BUILD_NOTES.md`, `BUGS.md`, `FEATURE_IDEAS.md`, `NAMING.md`, `OPTIMIZATIONS.md`, `REFACTOR_PLAN.md`, `ADJACENT_REALITY_USER_MANUAL.md`

## 1.0.2

### Refactoring

- **Single-translation-unit build**: All .cpp files included via main.cpp (no separate compilation units)
- **Modular file organization**: Renamed files to match modules (sequencer.cpp, ui.cpp, modes.cpp, melody.cpp, drums.cpp, synth.cpp, random.cpp)
- **Global state struct**: All scalar globals consolidated into `struct FirmwareState` in state.h
- **Named constants**: All magic numbers replaced with #defines in config.h
- **HAL layer**: Created hal.h/hal.cpp with static inline functions for hardware abstraction
- **Project rename**: All "FantasyMansion" → "AdjacentReality" in filenames, solution, project, comments

### Bug fixes

- **ISR reentrancy fixed**: Removed premature `hal_enable_interrupts()` from processStep() that allowed 20kHz ISR to fire mid-update, corrupting voice arrays
- **Voice array protection**: Wrapped Freq[]/Amp[] writes with cli()/sei() to prevent half-updated reads by 20kHz ISR
- **Amp resurrection fixed**: Dead voices (Amp[c] == 0) no longer come back to life from distortion
- **HH waveform precedence fixed**: Added parentheses to (sc >> 1) in hi-hat waveforms, restoring intended multiply pattern
- **triggerNote() cleaned up**: Removed unconditional sei() call, added clarifying parentheses to amplitude calculation
- **barLength bounds documented**: Added comment that barLength must not exceed DEFAULT_BAR_LENGTH (32)
- **ChordsB bass pattern completed**: ChordsB[] had only 16 of 32 entries initialized, leaving the second half of every bar silent for bass. Added the original author's commented-out second-half pattern (hits on steps 16, 20, 24, 28 with a different note than the first half)
- **Melody freqSelector out-of-bounds fixed**: Clamping logic only adjusted by one octave (+= 12 / -= 12), but freqSelector could range -21 to 49, leaving it negative after correction. On AVR-GCC, negative `% 32` stays negative, causing out-of-bounds PROGMEM reads from the Scale array. Replaced with `((x % 32) + 32) % 32` for proper wrapping
- **Bass freqSelector out-of-bounds fixed**: Same clamping bug as melody, with no check for > 31. Applied same modular arithmetic fix
- **getDrumSample division by zero fixed**: Default case `i % ((i >> drumParamA) & (i >> drumParamC))` could divide by zero when both shifts produce 0 for small `i`. Added zero-divisor guard
- **modeRecordMelody dead assignments removed**: Lines setting `allowKickChange=false` and `allowSnareChange=false` were immediately overwritten to `true` on the next two lines
- **processStep reentrancy guard added**: `playSequencerStep()` is called from both the watchdog ISR and `readInputs()` during sync receive. Without a guard, the watchdog ISR could re-enter `processStep()` before `cli()`, corrupting stepIndex and voice arrays
- **processStep reentrancy guard volatile**: `static bool running` changed to `static volatile bool running` — read by both ISR and main loop, must not be cached in register
- **generateMelodyPattern bar length fixed**: Used hardcoded `DEFAULT_BAR_LENGTH` (32) instead of `state.barLength`, generating notes beyond the played range when bar is shortened
- **removeNoteFromStep dead code removed**: `i = 99; break;` — the assignment was dead code since `break` exits immediately
- **playSequencerStep dead assignment removed**: `state.drumTimer = 1` was overwritten by `processStep()` and `playDrums()` before being read
- **Watchdog timer comment corrected**: Comment said "4 Hz interrupt" but WDP=000 is 16ms (~62.5 Hz). Updated to reflect actual rate and document tempoDivisor's role
- **LDR constrain upper bound fixed**: `constrain(rawLdr, 0, 1408)` — 1408 unreachable on 10-bit ADC (max 1023). Changed to 1023
- **generateChordPattern dead code removed**: `tripChords` random value was immediately overwritten with constant 3 — removed the dead `random()` call
- **sampleCounter sign fixed**: Changed from `int16_t` to `uint16_t` to prevent signed overflow undefined behavior when used as a timer in non-playing modes (wraps cleanly at 65535 instead of UB at 32767)

### Cue tone fixes

- **Cue tone pitch affected by mode state**: `playCueTone()` called `triggerNote(freq, CUE_TONE_OCTAVE, ...)` which fed into `synthesizeVoices()` where `(Freq + pitchBend) << octaveOffset` shifted the pitch. If octaveOffset was 0–1 (set by Autotune's LDR mapping), the cue tone played 1–2 octaves low. Fixed by pre-compensating: `triggerNote((freq >> octaveOffset) - pitchBend, 0, ...)` so the cue tone always sounds at its intended pitch without modifying any synth state
- **Cue tone negative freq fixed**: When pitchBend exceeds `freq >> octaveOffset`, the pre-compensation expression went negative, wrapping to a large unsigned value in Freq[]. Now clamped: uses unsigned comparison to ensure the result is never negative
- **cueToneFreqs moved to PROGMEM**: Stored in flash only (not copied to RAM at startup), saving ~8 bytes of RAM
- **Cue tone frequencies were wrong**: Stored values as `freq/8` produced oscillator increments of 1048–2040, which generated actual frequencies of 320–623 Hz (E4–D5) instead of the intended C6–C7 (1047–2093 Hz). Fixed by using the Scale array directly — cue tones now read `Scale[index]` for correct oscillator increments. Scale extended from 32→41 entries to cover F5–A6
- **Mode transition cue tone overwritten**: `selectMode()` set `playing = true` during transition, causing the watchdog ISR to call `processStep()` which overwrote the cue tone's voice data with sequencer notes. Removed the `playing = true` so the sequencer stays off during transition
- **Mode transition motif overwritten**: `synthesizeVoices()` in the 20kHz ISR wrote `hal_set_pwm_duty()` every 50µs, overwriting the motif's PWM value. Removed motif audio (kept LED only) so the cue tone is clean
- **Mode skip logic broken**: `handleModeTransition()` had `if (!tonesEnabled && mode == 1) mode = 3;` followed by `mode++`, which skipped modes 2 AND 3 (jumping 1→4) instead of just mode 2. Removed the broken skip entirely — all 8 modes are now reachable regardless of tonesEnabled
- **Dead mode 8 removed**: `case 8: break;` did nothing. Removed to shorten the mode cycle (8 presses back to mode 1 instead of 9) and recover ~10 bytes flash
- **Motif durations too short**: Step duration was 60ms (patterns 240–360ms total), making LED motifs nearly invisible. Increased to 150ms per step (300–750ms total)
- **Motif system removed**: The LED motif system (playModeMotif, updateMotif, motif arrays, modeMotifs lookup, motif state fields) consumed ~252 bytes of flash. Removed entirely — the simpler blocking blink on transition provides clearer feedback with minimal footprint
- **Cue tone octaveOffset and pitchBend bleed**: `octaveOffset` was still being written by mode functions (Autotune's LDR mapping, button1Released reset) during the cue tone, shifting its pitch. `pitchBend` was still being updated by `readInputs()` from the LDR. Both paths now guarded with `!cueToneActive`
- **Cue tone voice tracking added**: `cueToneVoice` field added to `FirmwareState` so the ISR can exempt the cue tone voice from `distortionAmount` decay. Without this, distortion could decay the cue tone faster than intended
- **Cue tone distortion bleed fixed**: `distortionAmount` and `playing` are now reset BEFORE calling `playCueTone()` in `handleModeTransition()`, preventing the WDT ISR from repopulating voices with distortion-modulated notes during the transition
- **Cue tone noteDecay tremolo bleed**: `noteDecay` was still being modulated by the LDR (in Autotune's button2 branch when `decayMode >= 3`) during the cue tone, causing the amplitude envelope to fluctuate — a tremolo bleed into the ding. Guarded with `!cueToneActive`
- **Cue tone noteDecay shift overflow**: `(1 << (noteDecay + 6))` overflows signed `int` when noteDecay ≥ 9 (shift ≥ 15). Changed to unsigned `1U` and moved the clamp to the write site: `noteDecay` is now capped at 9 when set from LDR, guaranteeing noteDecay+6 ≤ 15
- **Cue tone duration via distortion parameter**: Cue tone amplitude controlled by `CUE_TONE_DISTORTION` (=1, giving Amp=2048, ~102ms decay) instead of a separate Amp override. Saves ~4 bytes vs the old `CUE_TONE_AMPlITUDE` write
- **Cue tone processStep guard removed**: The `!cueToneActive` guard wrapping processStep()'s voice allocation block was redundant — `playing=false` prevents processStep() from being called during cue tones via playSequencerStep(). Removed to save ~8 bytes
- **distortionAmount guarded during cue tone**: `distortionAmount` was still being modulated by the LDR (in Autotune's else branch) during the cue tone. Guarded with `!cueToneActive`
- **modeMotifs[9] → [8]**: Array had 9 entries but only 8 modes existed (dead 9th slot). Shrunk to match NUM_MODES

### Features

- **A3: LDR delta tracking**: Tracks rate of change (delta and smoothed velocity). Used in Autotune (sweep up = distortion swell) and Button Solos (sweep up = drum intensity boost)
- **B6: Drum timbral variety**: 3 waveform variants per drum type (kick, snare, hihat). Timbre re-randomized when patterns generated
- **Per-mode cue tones**: Each mode transition plays a distinct pitch, letting you hear which mode you're entering. Modes 0, 2, 3, 4, 5 share a tight chromatic cluster (C6–E6). Modes 1, 6, 7 are spaced apart by fourths: mode 1 is a fourth below the cluster (A#5), mode 6 is a fourth above (A6), and mode 7 is two fourths below (F5). Pitch is pre-compensated for synth state (octaveOffset, pitchBend) so it always sounds correct regardless of the current mode's settings
- **Mode transition LED blink**: Blocking blink on LED during both-button hold — N blinks for mode N (1 blink for mode 0, 8 for mode 7). Cue tone plays through ISR during blink delays. ~25 bytes flash, 0 new state variables

### Dead code cleanup

- Removed `// addNote() disabled` comment and restructured conditional
- Documented all remaining original author comments in HICCUPS.md

## 1.0.1

### Bug fixes

- **ANTIMATTER sync-out restored**: PB4 is shared between tone PWM and sync output. The original `digitalWrite(syncPin, ...)` couldn't drive the pin because PWM hardware overrides the port register. The fix sends sync via `OCR1B = 255` (high) and `OCR1B = 0` (low), using the PWM duty cycle itself to produce clean sync levels on RING. MATTER sync (on PB1) is unaffected.
- **ANTIMATTER mode cycle unfrozen**: The `if (mode == 1) mode = 3; else mode = 0;` block was an off-by-one. The author intended to skip mode 2 (silent melody writer) by jumping from mode 1 to 3, then letting `mode++` land on 4 (Beat Writer). But the `else { mode = 0; }` collapsed every non-1 mode back to 1, trapping the cycle at 1↔4. Removing the else restores the intended forward progression through all drum/noise modes.
- **generateChords out-of-bounds fixed**: When `bools.Arp` is true, `chordStep` (0–2) is added to `i` before indexing `Chords[]`. Without `% barLength`, the index could reach 33 (e.g. `i=31 + chordStep=2`), writing past the 32-element array and corrupting adjacent globals (`ChordsB`, `arpLength`). The fix wraps with `% barLength` so the index stays in bounds.
- **generateBassLine shift fixed**: Bass note bit position clamped to 0–15 via `(16 - (bassval % 16)) % 16`, preventing negative or oversized shift amounts. The original formula `16 - (bassval % 16)` produces values 1–16, but `bitSet` on a 16-bit `unsigned int` requires bit positions 0–15. When `bassval % 16 == 0`, the result is 16 — an out-of-bounds bit position that corrupts the `ChordsB[]` entry (writes to bit 16, which doesn't exist in a 16-bit type). The outer `% 16` wraps this edge case to 0, mapping the root note to bit 0 instead. The fix changes the musical mapping slightly at the boundary: `bassval % 16 == 0` now maps to bit 0 (root) instead of the undefined bit 16, but since that was always undefined behavior, the new behavior is strictly correct. All other values (1–15) are unchanged.
- **Drum generator negative-shift fixed**: LDR value `x` is now floored at 0 before right-shifting in the drum waveform generators, preventing undefined behavior from negative shift amounts.
- **correctedx map overwrite fixed**: Commented out the dead `map()` line that was immediately overwritten by `constrain()`; kept the `constrain` call (behavior unchanged).
- **Slave boot floating inputs fixed**: Sync-detection pins now require 8 consecutive HIGH reads (~40ms) before locking onto a role, rejecting floating-pin noise. A 5s timeout falls back to Music Box master if no clock arrives.
- **Boot debounce button-stuck fixed**: The inner button-release wait in slave boot now times out after ~2s, preventing a hung state if one button is mechanically stuck.
- **Silent ding in ANTIMATTER fixed**: Added a `bools.ding` flag that briefly enables the tone generator for the both-press cue even when `tonesMode` is off. The flag auto-clears once all voice amplitudes decay to zero.
- **Static limbo drone fixed**: Added `t++` in the limbo branch of `modeSelect`, so the portB waveform evolves during the both-press hold instead of producing a constant tone.
- **ISR/main data race fixed**: Added `volatile` to `Freq[]`, `Amp[]`, and `Acc[]`, which are shared between the 20kHz timer ISR and the main loop. Without `volatile`, the compiler could cache stale values in registers, causing the ISR to read wrong frequencies or amplitudes.
- **Slave sync detection fixed**: Added `volatile` to `periodTimer`, which is incremented in the 20kHz ISR and read in the main loop's sync detection. Without `volatile`, the compiler could cache the count, causing stale period measurements, missed offbeats, and torn 16-bit reads.
- **Sequencer step counter fixed**: Added `volatile` to `selex`, which is incremented in the watchdog ISR and read in the main loop's drum pattern lookups. Without `volatile`, the compiler could cache the step index, causing occasional drum desync.
- **Voice allocator off-by-one fixed**: `playNoteNow()` cycled channels mod 3 instead of mod 4, permanently skipping channel 3. Fixed to `% Channels`, aligning with `playNextNote()` and restoring full 4-voice polyphony for the ding cue and portBplayer mode.
- **HH waveform precedence fixed**: Added parentheses to `(s >> 1)` in the hi-hat waveform calculation, restoring the intended multiply pattern used by BD and SD. Without them, `*` bound tighter than `>>`, turning a multiplication into a variable right-shift and making the hi-hat drastically quieter.
- **Hi-hat waveform counter fixed**: Added `volatile` to `s`, which is incremented in the 20kHz ISR and used in the hi-hat waveform calculation. Low audible impact (stale value off by at most 10 ticks), but eliminates undefined behavior.
- **Division by zero fixed**: `modulationSteps = random(0, 6)` could produce 0, causing `barTicker % modulationSteps` to crash. Changed to `random(1, 6)` to guarantee a nonzero divisor.
- **Erase-note flag stuck fixed**: `eraseNote` was only cleared in the bass branch of the watchdog ISR, not the melody branch. When erasing from the melody, the flag stayed true and the ISR erased a random note every 250ms until the user pressed a button. Now cleared in both branches.

### Dead code cleanup

- Commented out `addNote()` calls (empty function body) and `generateDists()` call (writes a variable no live code reads).
- Commented out unused globals and defines: `LED`, `xMode`, `octaveselect`, `diff`, `oldX`, `partTicker`, `songTicker`, `beatSeqSelex`, `arpLength`, `chordIntervalSelector`, `chordIntevals[]`, `dists`, `soloScales[]`, `TunePtr`, plus their live references where needed to maintain compilation.
- Commented out unused struct bitfields: `allowHHSeqMod`, `slowMo`, `portBticker` (and their initializers); simplified the `portB.cpp` condition that tested `portBticker` (always true).
- Commented out unreachable `break` statements after `return` and the unreachable `return 0` in `PortBs.cpp` `OutPut()`.
- Commented out dead forward declarations in `main.cpp`: `writeANote`, `generateDecayArray`, `trigOnChangeSolo`, `trigOnPurifySolo`, `chordSolo`, `xManip`, `generateOctaves`, `addNote`, `generateDists`.
- Commented out dead function bodies: `trigOnChangeSolo`, `trigOnPurifySolo`, `chordSolo`, `xManip`, `addNote`, `generateOctaves`, `generateDists`.
- Commented out `generateOctaves()` call in `refreshRandom.cpp` and `#include "xManip.cpp"` in `main.cpp`.
- Updated stale comments: `modeSelect.cpp` trailing note (buttonSolos is used), `allowTranspose` ("notUsed" → accurate description), `MELODY` ("CONTROLLED BY NOTHING YET" → notes where it's toggled).
