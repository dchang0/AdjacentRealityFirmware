
#include "config.h"
#include "state.h"
#include "melody.h"
#include "synth.h"

// ---------------------------------------------------------------------------
// MELODY / CHORD / BASS PATTERN GENERATION and CUE TONE.
//
// The firmware uses bitmaps to encode notes:
//   Chords[i]  — melody note bitmap (32-bit, bit 0 = highest note, bit 31 = lowest)
//   ChordsB[i] — bass note bitmap (16-bit, different bit-position encoding)
//
// SCALE: currentScale[] (in AdjacentReality.cpp) holds 15 scale-degree
// offsets in PROGMEM, organized as 3 scale types × 5 notes each.
// The sequencer converts scale degrees to oscillator increments via
// Scale[] lookup (done in sequencer.cpp, not here).
// This maps scale degrees to entries in the 38-entry chromatic Scale table.
//
// CHORD GENERATION: generateChordPattern() places 3-note chords at regular
// intervals (every `tripChords` steps). Each chord is built from the
// currentScale table: the root, a 3rd, and a 5th. If arpeggio is enabled,
// the chord notes are spread across consecutive steps instead of played
// simultaneously.
//
// MELODY GENERATION: generateMelodyPattern() scatters random notes at
// random intervals (1–5 steps apart). Notes are chosen from the current
// scale type and root.
//
// BASS GENERATION: generateBassPattern() places bass notes at regular
// intervals. The bass pattern cycles through scale degrees in a pattern
// determined by the period, creating a repeating bass line.
//
// CUE TONE: playCueTone() triggers a brief tone to confirm mode changes.
// The pitch is pre-compensated for the current octaveOffset and pitchBend
// so it always sounds correct regardless of the current mode's synth state.
// ---------------------------------------------------------------------------

void clearMelodyPattern() {
	for (int i = 0; i < state.barLength; i++) {
		Chords[i] = 0;
	}
}

// Remove the highest set note from a step. Used for "melody evolution" —
// every 4 bars, one random note is removed to keep the melody from
// becoming too dense over time.
void removeNoteFromStep(byte steppo) {
	for (int i = 0; i < 32; i++) {
		if (bitRead(Chords[steppo], i)) {
			bitClear(Chords[steppo], i);
			break;  // only remove the first (highest) note found
		}
	}
}

// ---------------------------------------------------------------------------
// Play the cue tone (the "ding" that confirms a mode change).
//
// The synth engine applies pitchBend and octaveOffset to ALL voices:
//   output_freq = (Freq + pitchBend) << octaveOffset
//
// To make the cue tone play at exactly the intended pitch, we pre-
// compensate by inverting the synth's math:
//   Freq = (target_freq >> octaveOffset) - pitchBend
//
// When the synth applies its transforms, they cancel out:
//   output = ((target >> oct) - pb + pb) << oct = target
//
// This lets us play a clean tone without modifying pitchBend or
// octaveOffset (which would affect the sequencer's voices).
// ---------------------------------------------------------------------------
void playCueTone(uint16_t freq) {
	state.cueToneVoice = state.nextVoice;
	// pitchBend is pre-cleared to 0 by the caller before calling playCueTone.
	// The ISR adds pitchBend back to Freq[c], so the oscillator stays stable.
	triggerNote(freq >> state.octaveOffset, 0, CUE_TONE_DISTORTION);
	state.flags.cueToneActive = true;
}

// ---------------------------------------------------------------------------
// Generate a chord pattern. Chords are placed at regular intervals
// (every `tripChords` steps, currently hardcoded to 3 = every 3rd step).
// Each chord is a triad built from the current scale: root, 3rd, 5th.
//
// If arpeggio is enabled, the 3 chord notes are spread across consecutive
// steps (played one at a time) instead of simultaneously. This creates
// an arpeggiated accompaniment pattern.
//
// chordBeatOffset randomly shifts the entire pattern in time so it
// doesn't always start on step 0.
// ---------------------------------------------------------------------------
void generateChordPattern() {
	byte tripChords = 3;  // chord repeat period (every 3 steps)
	byte chordBeatOffset = 0;

	// 50% chance to offset the chord pattern in time
	if (random(2)) {
		chordBeatOffset = (byte)random(0, 8);
	}
	for (int i = 0; i < state.barLength; i++) {
		if ((i + chordBeatOffset) % tripChords == 0) {
			// Build a 3-note chord from the scale.
			// chordStep 0 = root, 1 = 3rd, 2 = 5th.
			// (chordStep % 5) indexes into currentScale for the chord tones.
			// If arpeggio is enabled, each chord note lands on a different step.
			for (byte chordStep = 0; chordStep < 3; chordStep++) {
				bitSet(Chords[(i + (chordStep * state.flags.arpeggioEnabled)) % state.barLength], 31 - (state.scaleRoot + pgm_read_byte(&currentScale[(chordStep % 5) + (state.scaleType * 5)])));
			}
		}
	}
}

// Generate a sparse melody. Notes are placed at random intervals (1–5 steps
// apart) using random scale degrees from the current scale type.
// The `31 -` inversion maps scale degree 0 to the top of the bitmap,
// so lower degrees produce higher bit positions.
void generateMelodyPattern() {
	for (int i = 0; i < state.barLength; i = i + random(1, 6)) {
		bitSet(Chords[i], 31 - ((pgm_read_byte(&currentScale[random(5) + (state.scaleType * 5)]) + state.scaleRoot)));
	}
}

// ---------------------------------------------------------------------------
// Generate a bass pattern. Places notes at regular intervals (period 1–4
// steps). The bass cycles through scale degrees in a pattern that depends
// on the period:
//
//   bassScaleSelect = ((bassStep % (2*period)) * 2) % 5
//
// This creates a musically interesting pattern that avoids monotony —
// the bass line varies its degree over time while staying in the scale.
//
// The bit position in ChordsB is calculated as:
//   (16 - (bassval % 16)) % 16
// This maps the scale degree to a bit position, wrapping at 16 to stay
// within the 16-bit range of ChordsB.
// ---------------------------------------------------------------------------
void generateBassPattern() {
	clearBassPattern();
	byte period = random(1, 5);
	for (int i = 0; i < state.barLength; i = i + period) {
		byte bassStep = i / period;
		// Cycle through scale degrees based on the period
		byte bassScaleSelect = ((bassStep % (2 * period)) * 2) % 5;
		byte bassval = pgm_read_byte(&currentScale[bassScaleSelect + (state.scaleType * 5)]) + state.scaleRoot;
		// Map scale degree to bit position, wrapping at 16 bits
		bitSet(ChordsB[i], (16 - (bassval % 16)) % 16);
	}
}

void clearBassPattern() {
	for (byte i = 0; i < state.barLength; i++) {
		ChordsB[i] = 0;
	}
}
