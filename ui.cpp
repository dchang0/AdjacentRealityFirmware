
#include <util/delay.h>
#include "config.h"
#include "pins.h"
#include "state.h"
#include "sequencer.h"
#include "melody.h"
#include "drums.h"
#include "synth.h"
#include "ui.h"
#include "hal.h"

// ---------------------------------------------------------------------------
// USER INTERFACE — input reading, edge detection, and mode transitions.
//
// readInputs() is called every loop() iteration (~1–10 ms). It reads:
//   - The LDR (light sensor) via ADC
//   - Both buttons via direct port read
//   - Sync input (if in slave mode)
//
// The LDR value is processed into several forms:
//   lightLevel: calibrated, inverted, offset-corrected (0 = dark, ~600 = bright)
//   ldrVelocity: smoothed rate of change (for distortion/timbre control)
//   pitchBend: direct LDR value used to modulate synth pitch
//   scaledLight: raw ADC value (0–1023) used for note writing
// ---------------------------------------------------------------------------

void readInputs() {
	// --- LDR READING ---
	// The LDR is on ADC channel with a pull-up resistor. Wiring:
	//   VCC → LDR → ADC pin → pull-up resistor → GND
	// Bright light → LDR resistance drops → ADC reads LOW
	// Covering sensor → LDR resistance rises → ADC reads HIGH
	// We invert (1024 - raw) so that covering the sensor = higher value.
	state.rawLdr = analogRead(LDR_ADC_CHANNEL);
	state.lightLevel = 1024 - state.rawLdr;
	// Subtract the calibration offset (set by RV1 trimmer) so that
	// "fully covered" maps to ~0 and "fully open" maps to a positive value.
	state.lightLevel = state.lightLevel - LDR_OFFSET;
	if (state.lightLevel < 0) {
		state.lightLevel = 0;  // clamp: don't let calibration go negative
	}

	// --- LDR DELTA TRACKING ---
	// ldrDelta = how much the light level changed since last read (raw).
	// ldrVelocity = exponential moving average of the delta (smoothed).
	// This gives a "speed" reading: positive = hand moving away (brightening),
	// negative = hand covering (darkening). Used for distortion swell and
	// drum intensity control.
	state.ldrDelta = state.lightLevel - state.prevLdr;
	state.ldrVelocity = (state.ldrVelocity + state.ldrDelta) / 2;
	state.prevLdr = state.lightLevel;

	// --- PITCH BEND ---
	// When pitchBendActive is true (set by modes when a button is held),
	// the LDR value directly controls the synth pitch bend. This is frozen
	// during cue tones to prevent the ding from being affected.
	if (!state.flags.cueToneActive) {
		if (state.flags.pitchBendActive) {
			state.pitchBend = state.lightLevel;
		}
		else {
			state.pitchBend = 0;
		}
	}

	// scaledLight: raw ADC value clamped to 0–1023, used for note writing.
	// This preserves the full ADC range for pitch selection in record modes.
	state.scaledLight = constrain(state.rawLdr, 0, 1023);

	// --- BUTTONS ---
	// Buttons are active-low (pressed = LOW). We read the port directly
	// for speed, then invert so that pressed = true.
	byte pinState = hal_read_portb();
	state.flags.button1 = !bitRead(pinState, 0);  // MATTER on PB0
	state.flags.button2 = !bitRead(pinState, 3);  // ANTIMATTER on PB3

	// --- SYNC RECEIVE (slave mode) ---
	// When receiving external clock, we detect rising edges on the sync
	// pin to time our steps. The period between edges is measured and
	// used to calculate when to fire the next step.
	if (state.flags.receivingSync) {
		state.flags.syncInputHigh = (digitalRead(state.syncOutputPin));

		// Rising edge detected: a new clock pulse arrived
		if (state.flags.syncInputHigh && !state.flags.prevSyncInputHigh) {
			state.flags.prevSyncInputHigh = true;
			// Record the period (divide by 2 because we measure half-cycles)
			state.syncPeriod = state.periodTimer >> 1;
			state.periodTimer = 0;
			// Advance the sequencer on each sync pulse
			playSequencerStep();
		}
		// Falling edge or timeout: wait for the next rising edge
		else if (state.flags.prevSyncInputHigh && state.periodTimer > state.syncPeriod) {
			state.flags.prevSyncInputHigh = false;
		}
	}
}

// ---------------------------------------------------------------------------
// Edge detection for buttons. Converts the raw "is pressed" state into
// one-shot events: button1Pressed (just pressed), button1Released (just
// released), etc. These flags are cleared each loop iteration unless
// a new edge occurs.
// ---------------------------------------------------------------------------
void detectButtonEdges() {
	if (state.flags.button1 && !state.flags.prevButton1) {
		state.flags.button1Pressed = true;
		state.flags.prevButton1 = true;
	}
	else if (!state.flags.button1 && state.flags.prevButton1) {
		state.flags.prevButton1 = false;
		state.flags.button1Released = true;
	}
	else {
		state.flags.button1Pressed = false;
		state.flags.button1Released = false;
	}

	if (state.flags.button2 && !state.flags.prevButton2) {
		state.flags.button2Pressed = true;
		state.flags.prevButton2 = true;
	}
	else if (!state.flags.button2 && state.flags.prevButton2) {
		state.flags.prevButton2 = false;
		state.flags.button2Released = true;
	}
	else {
		state.flags.button2Pressed = false;
		state.flags.button2Released = false;
	}
}

// ---------------------------------------------------------------------------
// Mode transition gesture: press and hold both buttons.
//
// On press (both buttons go high simultaneously):
//   1. Kill all playing voices immediately
//   2. Reset distortion and stop the sequencer
//   3. Play the cue tone for the NEXT mode (pre-compensated for synth state)
//   4. Blink the LED N times where N = next mode number
//   5. Set transitioning=true to block mode functions during the hold
//
// On release (both buttons go low):
//   1. Advance state.mode to the next mode
//   2. Re-randomize parameters (unless user has recorded something)
//   3. Set modeFirstRun=true so the new mode's init code runs
//   4. Reset all pending write/erase flags
//
// The cue tone plays through the ISR during the LED blink loop, so
// the user hears the confirmation tone while seeing the blink count.
// ---------------------------------------------------------------------------
void handleModeTransition() {
	// Both buttons pressed simultaneously — start transition
	if (state.flags.button1 & state.flags.button2) {
		if (!state.flags.bothButtonsHeld) {
			// Kill all old voices.
			// No cli/sei needed: worst case the ISR reads a partially-zeroed
			// amplitude for one 50us sample — inaudible during a mode transition.
			Amp[0] = 0; Amp[1] = 0; Amp[2] = 0; Amp[3] = 0;
			// Reset synth state before playing the cue tone
			state.distortionAmount = 0;
			state.flags.playing = false;
			// Freeze pitchBend for the cue tone: clear pitchBendActive so
			// readInputs() sets pitchBend to 0, and set noteDecay to a safe
			// fixed value so the envelope doesn't wrap or go silent.
			state.flags.pitchBendActive = false;
			state.pitchBend = 0;
			state.noteDecay = 6;
			// Play the ding for the mode we're about to enter.
			// cueToneScaleIndex maps (mode+1)%8 to a Scale[] index,
			// so the pitch tells you which mode is coming next.
			playCueTone(pgm_read_word(&Scale[pgm_read_byte(&cueToneScaleIndex[(state.mode + 1) % NUM_MODES])]));
			state.flags.bothButtonsHeld = true;
			state.flags.transitioning = true;
			// Blink LED N times for visual confirmation of next mode.
			// The blocking delay loop is fine here — the cue tone plays
			// through the 20kHz ISR independently of the main loop.
			uint8_t nextMode = (state.mode + 1) % NUM_MODES;
			for (uint8_t i = 0; i <= nextMode; i++) {
				hal_drum_write(DRUM_SYNC_MASK, DRUM_SYNC_MASK);
				for (volatile uint8_t d = 0; d < 80; d++);
				hal_drum_write(DRUM_SYNC_MASK, 0);
				for (volatile uint8_t d = 0; d < 80; d++);
			}
		}
	}

	// Both buttons released — complete the transition
	if (state.flags.bothButtonsHeld && !(state.flags.button1 || state.flags.button2)) {
		state.flags.bothButtonsHeld = false;
		state.flags.transitioning = false;
		state.mode++;
		state.drumPatternIndex = (state.drumPatternIndex + 1) % 4;
		state.mode = state.mode % NUM_MODES;  // wrap: 8 → 0
		// Re-randomize if the user hasn't recorded their own content.
		// If preserveUserMelody is true, the user's creation survives.
		if (!state.flags.preserveUserMelody) {
			randomizeParameters();
		}
		// Reset all state for the new mode
		state.flags.playing = false;
		state.flags.pitchBendActive = false;
		state.drumTimer = 0;
		state.flags.modeFirstRun = true;  // triggers mode init code
		state.flags.recordingMelody = false;
		state.flags.writeNotePending = false;
		state.flags.eraseNotePending = false;
		state.flags.drumsMuted = false;
		state.flags.writeBeatPending = false;
		state.flags.eraseBeatPending = false;
		state.distortionAmount = 0;
		// Clear sync state from previous mode so it doesn't leak into the new one.
		// If the user was in slave mode (receivingSync=true), the WDT ISR
		// would skip step advancement.  If sendingSync was set, sync output
		// would fire on the wrong pin.
		state.flags.receivingSync = false;
		state.flags.sendingSync = false;
		state.syncOutputPin = 0;
		// Reset WDT counter so the new mode's tempo starts clean.
		// During the ~250 ms LED blink the WDT kept incrementing the
		// counter even though playing was false.  Without this reset
		// the first step would fire immediately on the next WDT tick.
		state.watchdogcounter = 0;
	}
}
