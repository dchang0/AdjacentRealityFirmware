
#include "config.h"
#include "state.h"
#include "melody.h"
#include "synth.h"
#include "drums.h"
#include "ui.h"
#include "random.h"

// ---------------------------------------------------------------------------
// MODES — each mode is a different "instrument" or performance behavior.
//
// selectMode() is called every loop() iteration. It reads state.mode
// (set during the mode-transition gesture) and calls the appropriate
// mode function. Each mode function runs continuously, reading button
// and LDR state to control its behavior.
//
// MODE 0/5 — AUTOTUNE (generative song + live performance)
//   The "default" mode. A generative composition plays automatically:
//   melody, bass, and drums are all algorithmically generated.
//   The user can interact live:
//     Hold MATTER: pitch-bend via LDR, octave via LDR brightness,
//       trigger drum samples. Release toggles drums on/off.
//     Hold ANTIMATTER: LDR controls octave (decayMode < 3) or
//       note decay (decayMode >= 3). Release cycles through
//       melody-only / bass-only / both playback modes.
//
// MODE 2 — MY FIRST MELODY (record notes with light)
//   Hold ANTIMATTER to write a note at the current step — the LDR
//   brightness selects the pitch. Hold MATTER to erase a note.
//   Notes are recorded into the Chords[] bitmap.
//
// MODE 3 — MY FIRST BASSLINE (record bass notes with light)
//   Same as Mode 2 but writes to ChordsB[] (bass register).
//
// MODE 4 — MY FIRST BEAT (build drum pattern with light)
//   Hold ANTIMATTER to place a drum hit at the next step — the LDR
//   brightness selects the drum (dark=kick, mid=hi-hat, bright=snare).
//   Hold MATTER to erase the next step.
//
// MODE 6 — BUTTON SOLOS (generative noise/texture performance)
//   No sequencer — everything is played live via buttons.
//   Hold MATTER: pitch-bend via LDR. Hold ANTIMATTER: trigger drum
//   samples with LDR-controlled timbre. Release: randomize all params.
//
// MODE 7 — PORTAL GLITCH (light-pitched notes over evolving noise)
//   Hold MATTER to re-randomize parameters and trigger a pitched note
//   from the LDR. The LDR value directly sets the note pitch.
//   Press ANTIMATTER to cycle through noise patterns.
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// MODE 0/5 — AUTOTUNE
//
// The generative composition engine. On first run, it generates a complete
// song: melody pattern, bass pattern, chord pattern, and drum patterns.
// The user's existing melody/beat (if any) is preserved if preserveUserMelody
// is set (from a previous recording session).
//
// Live performance controls:
//   MATTER held:    LDR controls pitch bend (continuous) and octave (stepped).
//                   Also triggers drum samples from the LDR timer.
//   ANTIMATTER held: If decayMode < 3, LDR controls octave.
//                    If decayMode >= 3, LDR controls note decay speed.
//   MATTER released: Toggle drums on/off. If no user beat, regenerate drums.
//   ANTIMATTER released: Cycle through playback modes:
//                    0 = melody + bass, 1 = melody only, 2 = bass only.
//
// distortionAmount: When neither button is held, the LDR velocity (rate of
//   hand movement) controls distortion. Sweeping up = more grit in the sound.
// ---------------------------------------------------------------------------
void modeAutotune() {
	if (state.flags.modeFirstRun) {
		state.flags.drumsMuted = false;
		state.flags.playing = true;
		state.flags.modeFirstRun = false;
		state.flags.allowSnareChange = true;
		state.flags.allowKeyTransposition = true;
		state.flags.allowKickChange = false;
		state.flags.allowNoteEvolution = true;
		if (!state.flags.preserveUserMelody) {
			// Fresh start: generate everything from scratch
			clearDrumPatterns();
			clearMelodyPattern();
			clearBassPattern();
			generateChordPattern();
			generateBassPattern();
			generateMelodyPattern();
			generateKickPattern();
			generateSnarePattern();
			if (random(2)) {
				generateHiHatPattern();  // 50% chance of hi-hats
			}
			state.flags.melodyEnabled = true;
			state.flags.bassEnabled = false;
		}
	}

	if (state.flags.button1) {
		// MATTER held: pitch-bend via LDR, octave via LDR brightness,
		// and trigger drum samples from the LDR-driven timer
		state.flags.pitchBendActive = true;
		if (!state.flags.cueToneActive) {
			state.octaveOffset = (state.lightLevel >> 7);  // 0..4
		}
		playDrumSample(state.drumTimer, state.drumPatternIndex);
		state.drumTimer++;
	}
	else if (state.flags.button2) {
		// ANTIMATTER held: octave or decay control via LDR
		if (state.decayMode < 3) {
			if (!state.flags.cueToneActive) {
				state.octaveOffset = (state.lightLevel >> 7);  // 0..4
			}
		}
		else {
			// Decay mode: LDR controls how fast notes fade out.
			// Higher noteDecay = faster fade = shorter notes.
			if (!state.flags.cueToneActive) {
				// Clamp to 9: noteDecay+6 must be ≤15 to avoid UB in processStep
				state.noteDecay = (state.lightLevel >> 6) > 9 ? 9 : (state.lightLevel >> 6);
			}
		}
	}
	else {
		// No buttons held: LDR velocity controls distortion
		state.flags.pitchBendActive = false;
		// LDR delta controls distortion: sweep up = more grit.
		// Guard with !cueToneActive to avoid any drift in the ISR during
		// the ding (cue tone voice is exempted, but keeps invariant clean).
		if (!state.flags.cueToneActive) {
			if (state.ldrVelocity > 10) {
				state.distortionAmount = state.ldrVelocity >> 3;
			}
			else {
				state.distortionAmount = 0;
			}
		}

		// MATTER released: toggle drums. If no user-created beat,
		// regenerate random drum patterns for variety.
		if (state.flags.button1Released) {
			if (!state.flags.cueToneActive) {
				state.octaveOffset = 2;  // reset to two octaves up
			}
			if (!state.flags.userCreatedBeat) {
				regenerateAllDrums();
			}
			state.flags.drumsMuted = !state.flags.drumsMuted;
		}

		// ANTIMATTER released: cycle playback mode
		// playMode 0: melody + bass | 1: melody only | 2: bass only
		if (state.flags.button2Released) {
			state.playMode++;
			if (state.playMode % 3 == 0) {
				state.flags.bassEnabled = true;
				state.flags.melodyEnabled = true;
				state.flags.pitchBendActive = false;
			}
			else if (state.playMode % 3 == 1) {
				state.flags.melodyEnabled = true;
				state.flags.bassEnabled = false;
			}
			else {
				state.flags.melodyEnabled = false;
				state.flags.bassEnabled = true;
			}
		}
	}
}

// ---------------------------------------------------------------------------
// MODE 2 — MY FIRST MELODY
//
// Record melody notes using the light sensor. Hold ANTIMATTER and wave
// your hand over the LDR to "paint" notes onto the current step. The
// LDR brightness maps to a scale degree (0–31 for melody). Hold MATTER
// to erase the note at the current step.
//
// preserveUserMelody: Once you record something, it's kept across mode
// changes. The generative engine won't overwrite your creation.
// ---------------------------------------------------------------------------
void modeRecordMelody() {
	if (state.flags.modeFirstRun) {
		if (!state.flags.preserveUserMelody) {
			// Re-roll a fresh song rather than wiping to silence. The manual
			// promises a new random composition on every mode change, and an
			// empty Chords[]/ChordsB[] left the unit with literally nothing
			// to play whenever the boot mode had disabled the drum generator
			// (MATTER master, or slave locked on the drum pin) — a dead unit
			// that only a power cycle could revive.
			clearMelodyPattern();
			generateMelodyPattern();
			generateBassPattern();
		}
		state.flags.drumsMuted = false;
		state.flags.playing = true;
		state.flags.userCreatedBeat = false;
		state.flags.preserveUserMelody = true;
		state.flags.recordingMelody = true;
		state.flags.allowKeyTransposition = false;
		state.flags.pitchBendActive = false;
		state.flags.allowNoteEvolution = true;
		state.flags.melodyEnabled = true;
		state.flags.bassEnabled = true;
		if (!state.flags.userCreatedBeat) {
			clearDrumPatterns();
			generateKickPattern();
		}
		state.flags.modeFirstRun = false;
	}

	state.flags.playing = true;
	if (state.flags.button1) {
		// MATTER held: erase note at current step
		state.flags.eraseNotePending = true;
	}
	else if (state.flags.button2) {
		// ANTIMATTER held: write note (pitch from LDR) at current step
		state.flags.writeNotePending = true;
	}
	else {
		state.flags.eraseNotePending = false;
		state.flags.writeNotePending = false;
	}
}

// ---------------------------------------------------------------------------
// MODE 4 — MY FIRST BEAT
//
// Record drum hits using the light sensor. Hold ANTIMATTER and the LDR
// brightness selects which drum to place at the next step:
//   Dark (lightLevel < 300):    kick
//   Mid  (lightLevel < 400):    hi-hat
//   Bright (lightLevel >= 400): snare
//
// Hold MATTER to erase all drum hits at the next step.
// The beat is written to (stepIndex + 1) so it appears on the NEXT step,
// giving a one-beat preview of where the hit will land.
// ---------------------------------------------------------------------------
void modeRecordBeat() {
	if (state.flags.modeFirstRun) {
		state.flags.userCreatedBeat = true;
		state.flags.preserveUserMelody = true;
		state.flags.allowKickChange = false;
		state.flags.allowSnareChange = false;
		state.flags.playing = true;
		state.flags.drumsMuted = false;
		state.flags.modeFirstRun = false;
		state.BDseq = 0;
		state.SDseq = 0;
	}
	if (state.flags.button2) {
		// ANTIMATTER: place drum at next step (type from LDR brightness)
		state.flags.writeBeatPending = true;
	}
	else if (state.flags.button1) {
		// MATTER: erase all drums at next step
		state.flags.eraseBeatPending = true;
	}
	else {
		state.flags.eraseBeatPending = false;
		state.flags.writeBeatPending = false;
	}
}

// ---------------------------------------------------------------------------
// MODE 6 — BUTTON SOLOS
//
// No sequencer — everything is played live via buttons, like drum pads.
// The sequencer is stopped (playing=false, drumsMuted=true).
//
// Hold ANTIMATTER: trigger drum samples (always running). The LDR brightness
//   controls drumParamA (timbre), and the LDR velocity controls drumParamC (grit).
// Hold MATTER: pitch-bend via LDR (always active).
// Release both: randomize all parameters (new timbre, new patterns).
//
// This mode is for improvisation — you create sound in real time rather
// than programming a pattern.
// ---------------------------------------------------------------------------
void modeButtonSolos() {
	state.flags.drumsMuted = true;
	state.flags.playing = false;

	if (state.flags.button2) {
		// LDR delta controls drum intensity: sweep up = more intense
		state.drumParamA = state.lightLevel / 10;
		if (state.ldrVelocity > 5) {
			state.drumParamC = state.ldrVelocity >> 2;
		}
		playDrumSample(state.drumTimer, state.drumPatternIndex);
		state.drumTimer++;
	}
	else if (state.flags.button1) {
		state.flags.pitchBendActive = true;
	}
	else {
		// No buttons: randomize and reset timer
		randomizeParameters();
		state.drumTimer = state.sampleCounter >> state.noisePatternIndex;
	}
	state.flags.pitchBendActive = true;
	// Always play a drum sample every loop iteration for continuous sound
	playDrumSample(state.drumTimer, state.drumPatternIndex);
}

// ---------------------------------------------------------------------------
// MODE 7 — NOISE GENERATOR (Portal Glitch)
//
// Experimental noise/texture mode. The sequencer runs but the output
// is pure noise manipulation rather than pitched notes.
//
// Press ANTIMATTER: cycle through 5 noise patterns (noisePatternIndex).
// Hold MATTER: re-randomize parameters and trigger a pitched note from
//   the LDR value — the LDR brightness directly maps to a note frequency.
// Neither: the drumTimer free-runs from sampleCounter for continuous
//   evolving noise.
// ---------------------------------------------------------------------------
void modeNoiseGenerator() {
	state.flags.drumsMuted = true;
	if (state.flags.button2Pressed) {
		// Cycle through noise patterns (5 available)
		state.noisePatternIndex++;
		state.noisePatternIndex = state.noisePatternIndex % 5;
	}
	else if (state.flags.button1) {
		// MATTER held: re-randomize params and trigger a pitched note
		// from the LDR value (lightLevel as frequency, octave-shifted)
		randomizeParameters();
		triggerNote(state.lightLevel, 1, 4);
	}
	else {
		// Free-running noise: drumTimer evolves via sampleCounter
		state.drumTimer = state.sampleCounter >> state.noisePatternIndex;
	}
	state.flags.pitchBendActive = true;
	playDrumSample(state.drumTimer, state.drumPatternIndex);
}

// ---------------------------------------------------------------------------
// selectMode — mode dispatcher. Called every loop() iteration.
//
// During a mode transition (both buttons held), drums are muted and
// no mode function runs. Once the buttons release, state.mode advances
// and the new mode starts.
//
// Mode mapping:
//   0, 5 → modeAutotune()  (same behavior, both are "default")
//   1    → modeAutotune()  (same as 0/5 — not skipped)
//   2    → modeRecordMelody() with melody recording
//   3    → modeRecordMelody() with bass recording
//   4    → modeRecordBeat()
//   6    → modeButtonSolos()
//   7    → modeNoiseGenerator()
// ---------------------------------------------------------------------------
void selectMode() {
	if (state.flags.transitioning) {
		// Both buttons held during transition — mute everything
		state.flags.drumsMuted = true;
	}
	else {
		switch (state.mode) {
		case 2:
			state.flags.recordMelodyNotBass = true;
			modeRecordMelody();
			state.flags.bassEnabled = true;
			break;

		case 3:
			state.flags.recordMelodyNotBass = false;
			modeRecordMelody();
			state.flags.bassEnabled = true;
			break;

		case 4:
			modeRecordBeat();
			break;

		case 6:
			modeButtonSolos();
			break;

		case 7:
			modeNoiseGenerator();
			break;

		default:
			// Modes 0, 1, 5 all fall through to Autotune
			modeAutotune();
			break;
		}
	}
}
