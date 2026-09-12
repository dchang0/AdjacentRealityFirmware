
#include "config.h"
#include "state.h"
#include "sequencer.h"
#include "melody.h"
#include "synth.h"
#include "drums.h"
#include "hal.h"

// ---------------------------------------------------------------------------
// SEQUENCER — the musical brain.
//
// The watchdog timer fires every 16ms (~62.5 Hz). Each tick, watchdogcounter
// increments by 2. When it reaches tempoDivisor, a step advances and the
// counter resets. This gives an effective step rate of:
//   62.5 Hz * 2 / tempoDivisor = steps per second
// Typical tempoDivisor values: 4–12 → 10–31 steps/sec.
//
// SWING: On odd-numbered steps (stepIndex % 2 == 1), the counter increments
// by 1 less (2-1=1 instead of 2), delaying that step by floor(tempoDivisor/2)
// WDT ticks (32–96ms for typical tempoDivisor values of 4–12).
// This creates a shuffle/swing feel — the classic "long-short" rhythm.
//
// STEP PROCESSING (processStep):
//   1. Handle any pending write/erase operations (melody, bass, drums)
//   2. Look up which notes to play from Chords[] and ChordsB[] bitmaps
//   3. Convert scale degrees to oscillator increments via Scale[] lookup
//   4. Advance step index and handle bar-end evolution
//
// NOTE ENCODING: Chords[] and ChordsB[] are bitmaps where
// each bit represents a scale degree (bit 0 = highest note, bit 31 = lowest). When a bit
// is set, that note plays on the current step. The firmware iterates all
// 32 possible scale degrees and checks each bit — this is how chords
// (multiple notes per step) work.
//
// SCALE SYSTEM: Scale[] is a table of oscillator increments (NOT frequencies
// in Hz). Each increment determines pitch via: freq_hz = increment * 20000 / 65536.
// The scale spans C6 to A6 chromatically (38 entries). freqSelector picks
// which entry to use based on the scale degree (Note) and modulation offset.
// ---------------------------------------------------------------------------

// Watchdog interrupt — the sequencer's clock source.
// Fires every 16ms. Counts ticks until tempoDivisor, then advances one step.
ISR(WDT_vect) {
	state.watchdogcounter += 2;
	// Swing: subtract 1 from the increment on odd steps, delaying them
	// by floor(tempoDivisor/2) WDT ticks. This creates a shuffle feel.
	state.watchdogcounter -= (state.stepIndex % 2) * state.flags.swingEnabled;

	// Only advance when not receiving external sync (slave mode uses its own clock)
	if (!state.flags.receivingSync && (state.watchdogcounter >= state.tempoDivisor)) {
		state.watchdogcounter = 0;
		playSequencerStep();
	}
}

// Called by both the WDT ISR (master) and readInputs() (slave sync receive).
void playSequencerStep() {
	// If we're the sync master, flag that a sync pulse needs to be sent
	if (state.flags.sendingSync) {
		state.flags.syncTickPending = true;
	}

	// Don't run the sequencer while the cue tone is still decaying.
	// The cue tone voice would be overwritten by processStep's voice
	// allocation, causing a trill or distortion.  The cue tone decays
	// in ~100 ms (CUE_TONE_DISTORTION=1 → Amp=2048, decay 1/sample
	// at 20 kHz).  Blocking here for that window is inaudible to the
	// user and prevents the voice collision.
	if (state.flags.playing && !state.flags.cueToneActive) {
		processStep();
		// Reset drum timer at each step so drums restart their decay envelopes
		state.sampleCounter = 4;
	}
}

// ---------------------------------------------------------------------------
// processStep — advance the sequencer by one step.
//
// This runs in two contexts:
//   1. From the WDT ISR (master mode) — interrupts are already disabled
//   2. From readInputs() during sync receive (slave mode) — needs cli/sei
//
// The reentrancy guard (static volatile bool running) prevents the ISR from
// re-entering processStep while it's already running from the sync path.
// ---------------------------------------------------------------------------
void processStep() {
	// Guard against reentrancy from sync-receive in main loop
	// volatile: read by both ISR and main loop, must not be cached in register
	static volatile bool running = false;
	if (running) return;
	running = true;

	// ---- PENDING WRITE/ERASE OPERATIONS ----
	// These flags are set by the main loop (readInputs) and processed here
	// so they happen in sync with the beat.

	// WRITE NOTE: User held the note-write button during this step.
	// The LDR value determines which note (scale degree) to place.
	if (state.flags.writeNotePending) {
		// LDR above threshold → set the octave bit (one octave higher)
		// and subtract the offset so the scale degree stays in range
		if (state.scaledLight > LDR_OCTAVE_THRESHOLD) {
			bitSet(state.octArray, state.stepIndex % 16);
			state.scaledLight = state.scaledLight - LDR_OCTAVE_OFFSET;
		}
		else {
			bitClear(state.octArray, state.stepIndex % 16);
		}
		// Right-shift the LDR value to get a scale degree:
		// Melody: >>5 gives 0–31 (full 32-note range)
		// Bass:   >>6 gives 0–15 (16-note range, lower register)
		if (state.flags.recordMelodyNotBass) {
			bitSet(Chords[state.stepIndex], state.scaledLight >> 5);
		}
		else {
			bitSet(ChordsB[state.stepIndex], state.scaledLight >> 6);
		}
		state.flags.writeNotePending = false;
	}

	// ERASE NOTE: User held the erase button — clear all notes on this step
	if (state.flags.eraseNotePending) {
		if (state.flags.recordMelodyNotBass) {
			Chords[state.stepIndex] = 0;
		}
		else {
			ChordsB[state.stepIndex] = 0;
		}
		state.flags.eraseNotePending = false;
	}

	// WRITE BEAT: User held the drum-write button. The LDR brightness
	// determines which drum: dark=kick, mid=hi-hat, bright=snare.
	// We write to (stepIndex + 1) so the hit appears on the NEXT step,
	// giving the user a one-step preview of where it'll land.
	if (state.flags.writeBeatPending) {
		if (state.lightLevel < KICK_LIGHT_MAX) {
			bitSet(state.BDseq, (state.stepIndex + 1) % 16);
		}
		else if (state.lightLevel < HAT_LIGHT_MAX) {
			bitSet(state.HHseq, (state.stepIndex + 1) % 16);
		}
		else {
			bitSet(state.SDseq, (state.stepIndex + 1) % 16);
		}
	}

	// ERASE BEAT: Clear all drum hits on the next step
	if (state.flags.eraseBeatPending) {
		bitClear(state.BDseq, (state.stepIndex + 1) % 16);
		bitClear(state.SDseq, (state.stepIndex + 1) % 16);
		bitClear(state.HHseq, (state.stepIndex + 1) % 16);
	}

	// ---- PLAY MELODY AND BASS NOTES ----
	// cli/sei protects Freq[]/Amp[] from 20kHz ISR reading half-updated values.
	// Voice allocation is safe during cue tones because playing=false prevents
	// processStep() from being called (playSequencerStep checks playing).
	cli();
	if ((state.flags.melodyEnabled || state.flags.bassEnabled) && state.syncOutputPin != 4) {
		// Iterate all 32 possible scale degrees. Each bit in Chords[]/ChordsB[]
		// for this step says whether that degree should sound.
		for (int Note = 0; Note < 32; Note++) {
			int freqBase = 31 - (Note - ((state.modulationInterval * (state.barCounter % state.transposeSteps)) * state.flags.allowKeyTransposition));
			// ---- MELODY ----
			if (state.flags.melodyEnabled) {
				if (bitRead(Chords[state.stepIndex], Note)) {
					int freqSelector = freqBase & 31;
					int oct = 1 << bitRead(state.octArray, state.stepIndex % 16);
					Freq[state.nextVoice] = (pgm_read_word_near(Scale + freqSelector)) >> (oct);
					Amp[state.nextVoice] = 1U << (state.noteDecay + 5);
					state.nextVoice = (state.nextVoice + 1) % (NUM_VOICES);
				}
			}
			// ---- BASS ----
			if (state.flags.bassEnabled) {
				if (bitRead(ChordsB[state.stepIndex], Note)) {
					int bassFreqSelector = (freqBase - 3) & 31;
					Freq[state.nextVoice] = (pgm_read_word_near(Scale + bassFreqSelector)) >> (3 + state.flags.bassOctaveDown);
					Amp[state.nextVoice] = 1U << (state.noteDecay + 6);
					state.nextVoice = (state.nextVoice + 1) % (NUM_VOICES);
				}
			}
		}
	}
	sei();

	// ---- ADVANCE STEP ----
	state.stepIndex++;

	// End of bar: wrap step index, occasionally evolve the snare pattern.
	// Snare changes happen ~75% of bar-ends (all odd bars, plus even bars
	// where random(2)!=1).
	if (state.stepIndex > state.barLength - 1) {
		state.barCounter++;
		if (!((state.barCounter % 2 == 0) && (random(2) == 1))) {
			if (state.flags.allowSnareChange) {
				generateSnarePattern();
			}
		}
		state.drumTimer = 0;
		state.stepIndex = 0;
	}

	// Every 4 bars: evolve the melody and kick pattern.
	// Melody evolution removes a random note (keeps things from getting stale).
	// Kick pattern regenerates every 4 bars for variety.
	if (state.barCounter > 3) {
		state.barCounter = 0;
		if (!state.flags.recordingMelody && state.flags.allowNoteEvolution) {
			removeNoteFromStep(random(state.barLength));
		}
		if (state.flags.allowKickChange) {
			generateKickPattern();
		}
	}
	state.drumTimer = 0;
	running = false;
}
