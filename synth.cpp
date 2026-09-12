// ---------------------------------------------------------------------------
// Synth engine — called at 20 kHz by Timer0 ISR.
//
// Generates audio by mixing up to 4 voices, each being a simple sawtooth
// oscillator with a decay envelope. The technique is from David Johnson-
// Davies' Digital Music Box (technoblogy.com, 2016).
//
// OSCILLATOR: Each voice has a 16-bit accumulator (Acc). Every sample,
// Freq[c] is added to Acc. When Acc overflows past 16 bits, it wraps —
// this overflow IS the sawtooth. The high byte (Acc >> 8) is a ramp 0→255.
//
// WAVEFORM SHAPING: The raw sawtooth is converted to a bipolar signal
// using XOR masking: the high bit of the ramp (Mask) flips the upper half
// of the envelope, turning a 0→255 ramp into a signal that alternates
// between positive and negative excursions around zero. The `(Mask & 1)`
// term adds a DC offset to keep it centered.
//
// AMPLITUDE: Env = Amp >> noteDecay controls loudness. Amp decays by 1
// each sample (or less with distortionAmount, which extends sustain),
// so notes naturally fade.
// Higher noteDecay = faster fade = shorter notes.
//
// MIXING: All 4 voices are summed into a signed byte. The result is
// offset to PWM_MIDPOINT (128) for the 8-bit PWM output.
//
// The output pin (TONE_PIN, PB4/OC1B) is driven by Timer1 in PWM mode.
// ---------------------------------------------------------------------------

#include "config.h"
#include "state.h"
#include "synth.h"
#include "hal.h"

void synthesizeVoices() {
	signed char Temp, Mask, Env, Note, Sum = 0;
	for (int c = 0; c < NUM_VOICES; c++) {
		// pitchBend and octaveOffset reads are safe during cue tones:
		// readInputs() freezes pitchBend when cueToneActive is true, and
		// mode functions guard octaveOffset writes with !cueToneActive.
		// The cue tone voice is pre-compensated so these values cancel out.

		// Advance the oscillator: add frequency to the accumulator.
		// Left-shifting by octaveOffset transposes up in octave steps
		// (offset 0 = normal, 1 = octave up, 2 = two octaves up).
		int fraqwancy = (Freq[c] + state.pitchBend);
		Acc[c] = Acc[c] + (fraqwancy << (state.octaveOffset));

		// Cue tone voice decays at 1/sample regardless of distortionAmount.
		// Other voices use distortionAmount but are all silent (Amp=0) during
		// cue tones because playing=false prevents processStep from running.
		if (state.flags.cueToneActive && c == state.cueToneVoice) {
			Amp[c] = Amp[c] ? (Amp[c] - 1) : 0;
		} else {
			Amp[c] = Amp[c] ? (Amp[c] - 1 + state.distortionAmount) : 0;
		}

		// Waveform: take the high byte of the accumulator (sawtooth ramp).
		Temp = Acc[c] >> 8;
		// Mask = sign bit of the ramp (0 or -1 via arithmetic shift).
		// Used to flip the waveform polarity each half-cycle, converting
		// the unipolar ramp into a bipolar signal.
		Mask = Temp >> 7;
		// Envelope: right-shift Amp by noteDecay to get the current volume.
		Env = Amp[c] >> state.noteDecay;
		// XOR flips the envelope when Mask=-1 (non-zero), creating the
		// bipolar shape. The (Mask & 1) term re-centers the output.
		Note = (Env ^ Mask) + (Mask & 1);
		// Sum all voices into one output sample.
		Sum = Sum + Note;
	}
	// Write to PWM. PWM_MIDPOINT (128) centers the signed sum in the
	// 0–255 PWM range, so the output swings equally above and below
	// the DC bias point. A capacitor on the output pin filters this
	// back to an analog audio waveform.
	hal_set_pwm_duty(Sum + PWM_MIDPOINT);

	// Check if all voices have finished decaying — if so, clear the
	// cue tone active flag so the main loop can resume normal operation.
	if (state.flags.cueToneActive) {
		bool done = true;
		for (int c = 0; c < NUM_VOICES; c++) {
			if (Amp[c] != 0) {
				done = false;
				break;
			}
		}
		if (done) {
			state.flags.cueToneActive = false;
		}
	}
}

// Trigger a note on the next available voice.
// Hz is an oscillator increment (not Hz in the musical sense) — see Scale[]
// in AdjacentReality.cpp for the mapping. The increment determines pitch:
// pitch = increment * 20000 / 65536 (at 20 kHz sample rate).
// oct shifts the increment right before storage (oct=0 = no shift).
// distortion controls initial amplitude: Amp = (1+distortion) << 10.
void triggerNote(int Hz, int oct, int distortion) {
	WDTCR |= 1 << WDIE;  // Re-enable WDT interrupt (may have been cleared)
	// No cli/sei needed: a torn 16-bit write here produces a one-sample
	// frequency/amplitude glitch at 20kHz (~50us) which is inaudible.
	Freq[state.nextVoice] = Hz >> oct;
	Amp[state.nextVoice] = (1 + distortion) << 10;
	// Round-robin voice allocation: advance to the next voice slot.
	state.nextVoice = (state.nextVoice + 1) % (NUM_VOICES);
}
