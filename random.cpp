
#include "state.h"
#include "melody.h"
#include "drums.h"
#include "random.h"

// ---------------------------------------------------------------------------
// RANDOMIZE PARAMETERS — re-roll all generative music parameters.
//
// Called on boot, mode transition, and in live modes (Button Solos release).
// Each parameter is randomized within a musically useful range:
//
//   noteDecay:       6 (moderate decay — not randomized to avoid extremes)
//   drumParamA/B/C:  control drum waveform shapes (timbre variety)
//   decayMode:       0–4, controls what ANTIMATTER does in Autotune
//   drumDecayLength: 50–299, how long drums ring
//   scaleRoot:       0–11, root note (chromatic transposition)
//   tempoDivisor:    8–22 (even), controls tempo: faster = lower value
//   scaleType:       0=minor, 1=major, 2=pentatonic
//   transposeSteps:  1–5, bars before transposing
//   modulationInterval: -7 to +6 scale degrees of transposition
//   swingEnabled:    0 or 1, shuffle feel
//   arpeggioEnabled: 0 or 1, spread chord notes across steps
//   bassOctaveDown:  0 or 1, extra octave down for bass
//
// If the user has created their own beat (userCreatedBeat), the drum
// patterns are preserved — only the tonal parameters change.
// ---------------------------------------------------------------------------
void randomizeParameters() {
	state.noteDecay = 6;

	state.drumParamA = random(1, 20);
	state.drumParamB = random(1, 20);
	state.drumParamC = random(5, 31);
	state.decayMode = random(5);
	state.drumDecayLength = random(50, 300);

	state.scaleRoot = random(0, 12);
	// <<1 ensures tempoDivisor is always even (smoother tempo changes)
	state.tempoDivisor = random(4, 12) << 1;

	state.drumPatternIndex = random(0, 4);  // which drum sample to play
	state.scaleType = random(0, 3);
	state.transposeSteps = random(1, 6);
	// modulationInterval is zeroed if key transposition is disabled
	state.modulationInterval = random(-7, 7) * state.flags.allowKeyTransposition;
	state.flags.swingEnabled = random(2);
	// arpeggioEnabled: 1/3 chance of being enabled (!(random(3)) is true only when random returns 0)
	state.flags.arpeggioEnabled = !(random(3));
	state.flags.bassOctaveDown = random(2);
	if (!state.flags.userCreatedBeat) {
		generateKickPattern();
		generateSnarePattern();
		generateHiHatPattern();
	}
}
