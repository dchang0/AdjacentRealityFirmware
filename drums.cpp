// ---------------------------------------------------------------------------
// Drum synthesis and pattern management.
//
// DRUM SYNTHESIS: Uses multiplicative time-decay formulas (different from
// the synth engine's overflow oscillator). The drum waveforms are from the
// original Fantasy Mansion firmware by Daniel Lacey-McDermott. They produce
// kick, snare, and hi-hat sounds by multiplying a time counter with various
// right-shifted versions of itself, creating complex timbres from simple
// integer math.
//
// PATTERN STORAGE: Each drum type uses a 16-bit shift register (BDseq,
// SDseq, HHseq) where each bit represents one step in a 16-step pattern.
// The step index selects which bit to read. Patterns are generated
// randomly on mode entry or re-roll.
//
// DRUM TIMER: `sampleCounter` (incremented at 20kHz) is right-shifted to
// create a slower time base for the drum decay envelopes. Different drum
// types use different shifts to get different decay lengths.
// ---------------------------------------------------------------------------

#include "state.h"
#include "drums.h"
#include "hal.h"

void clearDrumPatterns() {
	state.BDseq = 0;
	state.SDseq = 0;
	state.HHseq = 0;
}

void generateKickPattern() {
	// Simple two-variant kick pattern: either sparse or dense 16th notes.
	if (random(2)) {
		state.BDseq = 0B0010001000100010;  // kick on 2, 6, 10, 14 (offbeats)
	}
	else {
		state.BDseq = 0B1010010010010010;  // busier pattern
	}
	state.timbreKick = random(3);  // pick one of 3 kick timbres
}

void generateSnarePattern() {
	if (state.flags.drumsEnabled) {
		state.SDseq = 0;
		// Random snare placement: 1-in-6 chance per step
		for (int i = 15; i > -1; i--) {
			if (random(0, 6) == 0) {
				bitSet(state.SDseq, i);
			}
		}
	}
	state.timbreSnare = random(3);  // pick one of 3 snare timbres
}

void generateHiHatPattern() {
	if (state.flags.drumsEnabled) {
		state.HHseq = 0;
		// Two hi-hat density modes: straight 8ths or a syncopated pattern
		if (state.drumParamA > 10) {
			state.HHseq = 0B0101010101010101;  // every other step
		}
		else {
			state.HHseq = 0B1001001001001001;  // syncopated
		}
	}
	state.timbreHihat = random(3);  // pick one of 3 hi-hat timbres
}

void regenerateAllDrums() {
	generateKickPattern();
	generateSnarePattern();
	generateHiHatPattern();
}

// ---------------------------------------------------------------------------
// KICK WAVEFORMS — parameterized by time counter `t` (drumTimer).
//
// Each kick variant uses a different formula:
// case 0 "punchy": t * (t >> 3) >> (t >> 4)
// case 1 "boomier": t * (t >> 2) — no second shift, longer sustain
// case 2 "tight": t * (t >> 4) & (t >> 3) — AND narrows the envelope
// ---------------------------------------------------------------------------
static byte kickWave(int t) {
	switch (state.timbreKick) {
	case 0:  return t * (t >> 3) >> (t >> 4);               // punchy
	case 1:  return t * (t >> 2);                            // boomier
	default: return t * (t >> 4) & (t >> 3);                // tight
	}
}

// ---------------------------------------------------------------------------
// SNARE WAVEFORMS — same technique as kick but with added noise.
//
// `noise` is SD_NOISE_OFFSET (a large negative constant from the original
// firmware) that gets added to the time counter. This creates a harsh,
// noisy texture on top of the pitched component. The negative offset
// causes the multiplication to produce a buzzing/cracking character
// rather than a clean tone.
// ---------------------------------------------------------------------------
static byte snareWave(int t, int noise) {
	int s = t + noise;
	switch (state.timbreSnare) {
	case 0:  return s * (t >> 3) >> (t >> state.lightLevel); // original
	case 1:  return s * (t >> 2) >> (t >> state.lightLevel); // boomier
	default: return s * (t >> 3) & (t >> 2);                 // tight
	}
}

// ---------------------------------------------------------------------------
// HI-HAT WAVEFORMS — parameterized by raw sample counter `sc`.
//
// Hi-hat uses a higher time base (raw sample counter instead of drumTimer)
// so it cycles faster, producing a metallic/bright character. The formula
// mixes two shifted versions of the counter with OR or XOR to create
// inharmonic sidebands — this is what makes it sound like cymbals/hi-hats
// rather than a pitched tone.
//
// case 0 "original": OR combines two spectral components
// case 1 "darker": deeper shift (sc>>2) for a lower, duller timbre
// case 2 "metallic": XOR creates a more complex, brighter spectrum
//
// Cases 0 and 1 use `>> 7` and `>> 9` to set the base frequency.
// Case 2 ("metallic") uses `>> 6` and `>> 8` for a brighter spectrum.
// state.lightLevel modulates one component via the LDR, so covering the
// sensor changes the hi-hat timbre in real time.
// ---------------------------------------------------------------------------
static byte hihatWave(int sc) {
	switch (state.timbreHihat) {
	case 0:  return (sc >> 1) * (sc >> 1 >> 7 | sc >> state.lightLevel >> 9); // original
	case 1:  return (sc >> 2) * (sc >> 2 >> 7 | sc >> state.lightLevel >> 9); // darker
	default: return (sc >> 1) * (sc >> 1 >> 6 ^ sc >> state.lightLevel >> 8); // metallic
	}
}

// Play active drums for the current step. Reads the pattern shift registers
// to decide which drums fire, then calls the waveform generators.
void playDrums() {
	if (!state.flags.drumsMuted) {
		// drumTimer = sampleCounter >> 2 gives a 5 kHz time base (4x slower
		// than the 20 kHz sample rate), used as the decay envelope for drums.
		state.drumTimer = state.sampleCounter >> 2;

		if (state.drumTimer < state.drumDecayLength) {
			// Kick: fire if the pattern bit is set for this step
			if (bitRead(state.BDseq, state.stepIndex % 16) == 1) {
				hal_drum_write(state.drumMask, kickWave(state.drumTimer));
			}
			// Snare: fire if pattern bit set AND within the first half of
			// the decay envelope (so snare cuts off before kick)
			if (bitRead(state.SDseq, state.stepIndex % 16) == 1 && state.drumTimer < (state.drumDecayLength >> 1)) {
				hal_drum_write(state.drumMask, snareWave(state.drumTimer, SD_NOISE_OFFSET));
			}
			// Hi-hat: always fires if pattern bit set (no decay cutoff)
			if (bitRead(state.HHseq, state.stepIndex % 16) == 1) {
				hal_drum_write(state.drumMask, hihatWave(state.sampleCounter));
			}
		}
		else {
			// Decay expired — silence the drum output
			hal_drum_write(state.drumMask, 0);
		}
	}
}

// Play a single drum sample at a given time. Used by modeButtonSolos
// for real-time drum pad triggering (not sequenced).
void playDrumSample(int i, byte selector) {
	int OUT = getDrumSample(i, selector);
	hal_drum_write(state.drumMask, OUT);
}

// Generate a drum sample at time index `i`. selector picks the algorithm.
// These are the same formulas as the kick/snare/hihat waveforms but
// parameterized by drumParamA/B/C (LDR-controlled) for live timbre control.
byte getDrumSample(long i, byte selector) {
	switch (selector) {
	case 1:
		return i * (i >> state.drumParamA | i >> state.drumParamB);
	case 2:
		return i * (i >> state.drumParamA | i >> state.drumParamB) & (state.drumParamC * 3);
	case 3:
		return (i & i >> state.drumParamA | i >> (state.drumParamB / 2));
	default: {
		// Division-based waveform: creates metallic/ring-mod tones.
		// If the denominator is zero (early in the decay), return 0
		// to avoid division by zero.
		byte denom = (i >> state.drumParamA) & (i >> state.drumParamC);
		return denom ? i % denom : 0;
	}
	}
}
