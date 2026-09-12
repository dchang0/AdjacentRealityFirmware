/* Adjacent Reality — Generative Synthesizer Firmware
By dchang0
Based on Fantasy Mansion By Daniel Lacey-McDermott
CC BY 4.0
Licensed under a Creative Commons Attribution 4.0 International license: 
http://creativecommons.org/licenses/by/4.0/

Tone generation code based on Digital Music Box v2 by David Johnson-Davies
- www.technoblogy.com - 16th March 2016

This project is meant for Microchip or Atmel Studio 7.x

ARCHITECTURE OVERVIEW:
This firmware runs on an ATtiny85 (8KB flash, 512B RAM) at 8 MHz.
It is a single-translation-unit build: main.cpp #includes all .cpp files.

Two ISRs drive the audio:
  Timer0 (20 kHz): calls synthesizeVoices() — the sample-accurate synth engine
  Watchdog (~62 Hz): calls playSequencerStep() — advances the musical clock

The main loop (~1–10 ms) handles:
  - Reading buttons and LDR
  - Mode-specific behavior (modes.cpp)
  - Mode transitions (ui.cpp)
  - Sync output (if master)

KEY DATA FLOW:
  LDR → readInputs() → lightLevel, pitchBend, ldrVelocity
  Buttons → detectButtonEdges() → pressed/released events
  Mode functions → write to Freq[], Amp[] (voice parameters)
  WDT ISR → processStep() → reads Chords[], writes Freq[], Amp[]
  Timer0 ISR → synthesizeVoices() → reads Freq[], Amp[], Acc[] → PWM output
*/

#include <avr/pgmspace.h>
#include <avr/wdt.h>

#include "config.h"
#include "pins.h"
#include "state.h"
#include "sequencer.h"
#include "melody.h"
#include "synth.h"
#include "drums.h"
#include "ui.h"
#include "modes.h"
#include "random.h"
#include "hal.h"

/*Required modes
L+R = wait for sync signal and choose
just L send sync on drums
just R send sync on tones
*/

// ---------------------------------------------------------------------------
// Global state instance — single source of truth for all mutable firmware state.
// Large arrays remain as file-scope globals (too big for the struct on ATtiny85).
// ---------------------------------------------------------------------------
struct FirmwareState state = {
	// --- SEQUENCER ---
	0,                  // stepIndex:      current step (0–barLength-1)
	0,                  // sampleCounter:  free-running sample counter (20 kHz)
	10,                 // periodTimer:    measures time between sync pulses
	1,                  // barCounter:     counts bars for pattern evolution
	0,                  // drumTimer:      time since last drum trigger (for decay)
	DEFAULT_BAR_LENGTH, // barLength:      steps per bar (32)
	8,                  // tempoDivisor:   controls tempo (higher = slower)
	0,                  // watchdogcounter: WDT tick accumulator for tempo
	0,                  // noisePatternIndex: selects which noise pattern to use

	// --- LDR / SENSOR ---
	0,  // rawLdr:           raw ADC reading (0–1023, inverted)
	0,  // lightLevel:       calibrated, inverted (0=dark, ~600=bright)
	0,  // scaledLight:      raw ADC for note writing (0–1023)
	0,  // pitchBend:        LDR value used to modulate synth pitch
	0,  // prevLdr:          previous lightLevel (for delta calculation)
	0,  // ldrDelta:         rate of change of lightLevel
	0,  // ldrVelocity:      smoothed LDR delta (exponential moving average)

	// --- MUSIC PARAMETERS ---
	12, // scaleRoot:        root note offset into the Scale table
	0,  // scaleType:        0=minor, 1=major, 2=pentatonic (indexes currentScale)
	0,  // modulationInterval: transposition amount (in scale degrees)
	4,  // transposeSteps:   how many bars before transposing
	2,  // octaveOffset:     transposes all voices by this many octaves
	0,  // distortionAmount: adds grit to voice decay (0=clean, higher=gritty)
	4,  // decayMode:        controls what ANTIMATTER does (octave vs decay)
	9,  // noteDecay:        controls note fade speed (higher=faster fade)
	0,  // playMode:         cycles through melody/bass/both playback
	0,  // octArray:         per-step octave bit flags (bit N = step N octave down)

	// --- DRUMS ---
	0,              // drumParamA:       controls drum waveform timbre (0–20)
	0,              // drumParamB:       controls drum waveform timbre (0–20)
	0,              // drumParamC:       controls drum waveform timbre (0–30)
	4,              // drumPatternIndex: selects which drum sample to play
	500,            // drumDecayLength:  how long drums ring (in drumTimer ticks)
	DRUM_SYNC_MASK, // drumMask:         GPIO bitmask for drum/sync output
	0b1000000010000000U, // BDseq: kick pattern (16 steps, 1 bit each)
	0b1111111111111111U, // SDseq: snare pattern (all steps on initially)
	0b0000000000000000U, // HHseq: hi-hat pattern (all steps off initially)
	0,              // timbreKick:       which kick waveform variant (0–2)
	0,              // timbreSnare:      which snare waveform variant (0–2)
	0,              // timbreHihat:      which hi-hat waveform variant (0–2)

	// --- SYNC ---
	0,  // syncOutputPin:  which pin to use for sync (0 = none, 1 = PB1, 4 = PB4)
	1,  // syncPeriod:     measured period between sync pulses

	// --- VOICE ALLOCATION ---
	0,  // nextVoice:    round-robin index for the next voice to trigger
	0,  // cueToneVoice: which voice is playing the cue tone (for ISR exemption)

	// --- MODE ---
	1,  // mode:         current mode (0–7), starts in Autotune

	// --- FLAGS (compile-time init) ---
	// Non-zero initial values by index: recordMelodyNotBass(1),
	// prevSyncInputHigh(6), tonesEnabled(10), drumsEnabled(11),
	// allowKickChange(12), allowSnareChange(13), modeFirstRun(21),
	// bassEnabled(35), melodyEnabled(36), arpeggioEnabled(37).
	// All others (including bassOctaveDown) start at 0.
	{0,1,0,0,0,0,1,0,0,0,1,1,1,1,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,0},
};

// ---------------------------------------------------------------------------
// Large arrays — kept as file-scope globals (too big for the struct).
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// SCALE TABLE — oscillator increments for musical pitches.
//
// These are NOT frequencies in Hz — they are the increment values added to
// the 16-bit accumulator each sample. The actual pitch in Hz is:
//   freq_hz = Scale[n] * 20000 / 65536
//
// The table spans ~208 Hz (G#3) to ~1760 Hz (A6) — 38 entries
// covering roughly 3 octaves. Each entry is spaced by a semitone ratio
// of 2^(1/12) ≈ 1.0595.
//
// Index 0  = ~208 Hz (G#3, below C6 — used for bass)
// Index 28 = 1048 Hz (C6)
// Index 37 = 1760 Hz (A6 — highest cue tone)
//
// The sequencer indexes into this table via freqSelector, which maps
// scale degrees (from Chords[] bitmaps) to table positions.
// ---------------------------------------------------------------------------
const uint16_t PROGMEM Scale[38] = {
  681,  721,  764,  809,  858,  909,  963, 1020, 1080, 1145, 1213, 1286,
  1362, 1429, 1529, 1620, 1716, 1818, 1926, 2041, 2162, 2291, 2427, 2571,
  2724, 2886, 3058, 3240, 3432, 3636, 3850, 4080,
  4320, 4577, 4850, 5136, 5440, 5767
};

// ---------------------------------------------------------------------------
// SCALE INTERVALS — defines the musical scales used by the generative engine.
//
// 15 entries organized as 3 scale types × 5 notes each:
//   [0..4]  = minor scale:   root, minor 3rd, perfect 5th, minor 7th, octave
//   [5..9]  = major scale:   root, major 3rd, perfect 5th, major 7th, octave
//   [10..14] = pentatonic:   root, major 2nd, perfect 4th, perfect 5th, major 6th
//
// These are semitone offsets from the root. The chord and melody generators
// use these to build chords (root + 3rd + 5th) and scatter random notes.
// ---------------------------------------------------------------------------
const byte currentScale[15] PROGMEM = {
  0, 3, 7, 10, 12,  // minor:   C, Eb, G, Bb, C
  0, 4, 7, 11, 12,  // major:   C, E, G, B, C
  0, 2, 5, 7, 9     // pentatonic: C, D, F, G, A
};

// ---------------------------------------------------------------------------
// CHORDS — melody note bitmap. Each element is one step in the bar.
// Each bit represents a scale degree (bit 0 = highest note, bit 31 = lowest).
// When a bit is set, that note plays on that step. Multiple bits = chord.
// Stored as unsigned long (32 bits) to cover the full 32-note range.
// ---------------------------------------------------------------------------
unsigned long Chords[DEFAULT_BAR_LENGTH] = {
  0B00000000000000010000000000000000,
  0B00000000000000000000000000000000,
  0B00000000000000000000000000000000,
  0B00000000000000000000000000000000,
  0B00000000000000010000000000000000,
  0B00000000000000000000000000000000,
  0B00000000000000000000000000000000,
  0B00000000000000000000000000000000,
  0B00000000000000010000000000000000,
  0B00000000000000000000000000000000,
  0B00000000000000000000000000000000,
  0B00000000000000000000000000000000,
  0B00000000000000010000000000000000,
  0B00000000000000000000000000000000,
  0B00000000000000000000000000010000,
  0B00000000000000000000000000000000,
  0B00000000000000010000000000000000,
  0B00000000000000000000000000000000,
  0B00000000000000000000000000010000,
  0B00000000000000000000000000000000,
  0B00000000000000010000000000000000,
  0B00000000000000000000000000000000,
  0B00000000000000000000000000010000,
  0B00000000000000000000000000000000,
  0B00000000000000010000000000000000,
  0B00000000000000000000000000000000,
  0B00000000000000000000000000010000,
  0B00000000000000000000000000000000,
  0B00000000000000010000000000000000,
  0B00000000000000000000000000000000,
  0B00000000000000000000000000010000,
  0B00000000000000000000000000000000
};

// ---------------------------------------------------------------------------
// BASS NOTE BITMAP — same encoding as Chords but for the bass register.
// Uses unsigned int (16 bits) since bass notes span a smaller range.
// ---------------------------------------------------------------------------
unsigned int ChordsB[DEFAULT_BAR_LENGTH] = {
  0B0000000000001000, 0, 0,
  0B0000000000001000, 0, 0,
  0B0000000000001000, 0, 0, 0, 0, 0, 0, 0, 0, 0,
  0B0000000100000000, 0, 0, 0,
  0B0000000100000000, 0, 0,
  0B0000000100000000, 0, 0,
  0B0000000100000000, 0, 0,
  0B0000000100000000, 0
};

// ---------------------------------------------------------------------------
// VOICE STATE — shared between main loop and 20kHz ISR.
//
// Acc[]  — 16-bit accumulator for each voice. Overflow = sawtooth cycle.
// Freq[] — oscillator increment added to Acc[] each sample. Determines pitch.
// Amp[]  — current amplitude. Decays by 1 each sample (or more with distortion).
//
// All three are volatile because the ISR reads them while the main loop writes.
// On ATtiny85 (8-bit AVR), 16-bit writes are NOT atomic — they compile to two
// byte-stores. A torn write can cause a one-sample glitch if the ISR fires
// between the two stores. This is accepted as inaudible at 20 kHz.
// ---------------------------------------------------------------------------
volatile unsigned int Acc[NUM_VOICES];
volatile unsigned int Freq[NUM_VOICES];
volatile unsigned int Amp[NUM_VOICES];


// ---------------------------------------------------------------------------
// SETUP — runs once at power-on.
//
// 1. Configure GPIO pins (buttons as input with pull-ups, outputs for tone/drum)
// 2. Detect boot mode from button state:
//    - Both buttons held: slave mode — wait for external sync clock
//    - MATTER held: master mode — send sync on drum pin
//    - ANTIMATTER held: master mode — send sync on tone pin
//    - Neither: normal Autotune mode
// 3. Seed the random number generator from the LDR (ambient light noise)
// 4. Initialize Timer1 for PWM tone output, Timer0 for 20 kHz sample clock
// 5. Initialize the watchdog timer for the sequencer clock (~62 Hz)
// 6. Generate initial drum patterns and randomize parameters
// ---------------------------------------------------------------------------
void setup() {
	pinMode(TONE_PIN, OUTPUT);
	pinMode(DRUM_PIN, OUTPUT);
	pinMode(BTN_1, INPUT_PULLUP);
	pinMode(BTN_2, INPUT_PULLUP);
	pinMode(LDR_PIN, INPUT_PULLUP);

	///BOOTMODES///
	// Both buttons held at boot: SLAVE MODE.
	// Set pins to INPUT so we can detect incoming sync pulses.
	// Poll both pins for up to 5 seconds, looking for a clock signal.
	// Whichever pin shows a pulse first becomes the sync input.
	// If no clock arrives, fall back to "Music Box" master mode.
	if (!digitalRead(BTN_1) && !digitalRead(BTN_2)) {
		pinMode(TONE_PIN, INPUT);
		pinMode(DRUM_PIN, INPUT);
		byte lockCount = 0;
		unsigned int waitCount = 0;
		while (state.syncOutputPin == 0 && waitCount < SYNC_TIMEOUT) {
			if (digitalRead(DRUM_PIN)) {
				if (++lockCount >= SYNC_LOCK_COUNT) {
					state.flags.drumsEnabled = false;
					state.flags.receivingSync = true;
					state.syncOutputPin = DRUM_PIN;
					pinMode(TONE_PIN, OUTPUT);
					state.drumMask = B00000000;
				}
			}
			else if (digitalRead(TONE_PIN)) {
				if (++lockCount >= SYNC_LOCK_COUNT) {
					state.flags.tonesEnabled = false;
					state.syncOutputPin = TONE_PIN;
					pinMode(DRUM_PIN, OUTPUT);
					state.flags.receivingSync = true;
					state.flags.drumsEnabled = true;
					state.mode = 4;
				}
			}
			else {
				lockCount = 0;
			}
			delay(5);
			waitCount++;
			delay(5);
		}
		if (state.syncOutputPin == 0) {
			pinMode(TONE_PIN, OUTPUT);
			pinMode(DRUM_PIN, OUTPUT);
			state.flags.drumsEnabled = false;
			state.drumMask = DRUM_SYNC_MASK;
		}
	}
	// MATTER held at boot: MASTER MODE — melody on RIGHT (RING), sync on LEFT (TIP)
	else if (!digitalRead(BTN_1)) {
		state.flags.drumsEnabled = false;
		state.flags.sendingSync = true;
		state.syncOutputPin = 1;
		state.drumMask = B00000000;
	}
	// ANTIMATTER held at boot: MASTER MODE — drums on LEFT (TIP), sync on RIGHT (RING)
	else if (!digitalRead(BTN_2)) {
		state.flags.drumsEnabled = true;
		state.flags.tonesEnabled = false;
		state.flags.sendingSync = true;
		state.syncOutputPin = 4;
		state.drumMask = DRUM_SYNC_MASK;
	}
	// Neither button: NORMAL BOOT — Autotune mode, no sync
	else {
	}

	// --- RANDOM SEED ---
	// Seed the PRNG from the LDR (ambient light + electrical noise) and a
	// fixed firmware seed. This ensures different compositions each boot
	// while keeping reproducibility if the same light conditions recur.
	int randSeed = (analogRead(LDR_ADC_CHANNEL));
	digitalWrite(DRUM_PIN, HIGH);
	randSeed = randSeed + (analogRead(LDR_ADC_CHANNEL)) + FIRMWARE_SEED;
	randomSeed(randSeed);

	TIMSK = 0;  // disable all timer interrupts during setup

	// --- CLOCK SPEED TOGGLE (16 MHz vs 8 MHz) ---
	// For 16 MHz: uncomment the next line AND switch to TIMER0_COMPARE_16MHZ below.
	// For  8 MHz: leave it commented out AND use TIMER0_COMPARE_8MHZ below.
	//hal_init_pll();

	// Timer1: PWM output for tone generation (8-bit, Phase-correct mode)
	hal_init_timer1();
	hal_set_pwm_duty(PWM_MIDPOINT);  // start at 50% duty (silence after cap)

	// --- CLOCK SPEED: change the constant below to match your clock ---
	//   16 MHz PLL  -> use TIMER0_COMPARE_16MHZ  (and uncomment hal_init_pll() above)
	//    8 MHz RC   -> use TIMER0_COMPARE_8MHZ   (and keep hal_init_pll() commented)
	// Timer0: 20 kHz sample clock for the synth engine
	hal_init_timer0(TIMER0_COMPARE_8MHZ);
	TIMSK = 1 << OCIE0A;  // enable Timer0 compare match A interrupt
	OSCCAL += 2;  // nudge the internal RC oscillator for better tuning

	hal_init_watchdog();  // WDT for the sequencer clock (~62 Hz)
	hal_enable_interrupts();

	generateSnarePattern();
	generateHiHatPattern();
	randomizeParameters();
}


// ---------------------------------------------------------------------------
// MAIN LOOP — runs continuously (~1–10 ms per iteration).
//
// The loop order matters:
//   1. playDrums(): output drum audio if enabled (drives the drum pin)
//   2. readInputs(): read LDR and buttons, update sensor state
//   3. detectButtonEdges(): convert raw button state to press/release events
//   4. handleModeTransition(): detect both-button-hold gesture
//   5. sync output: send clock pulses if we're the sync master
//   6. selectMode(): run the current mode's behavior (writes to voices)
//
// The 20 kHz ISR runs independently of this loop, continuously generating
// audio from the current voice state (Freq[], Amp[], Acc[]).
// ---------------------------------------------------------------------------
void loop() {
	// Play drum audio on the drum pin (driven by sampleCounter timing)
	if (state.flags.drumsEnabled) {
		playDrums();
	}
	// Read sensor and button state
	readInputs();
	// Detect press/release edges for one-shot events
	detectButtonEdges();
	// Handle both-button mode transition gesture
	handleModeTransition();

	// --- SYNC OUTPUT ---
	// If we're the master and a sync tick is pending, output a clock pulse.
	// The pulse alternates high/low on even/odd steps to create a square wave.
	if (state.flags.syncTickPending) {
		byte level = !((state.stepIndex + 1) % 2);
		if (state.syncOutputPin == TONE_PIN && !state.flags.tonesEnabled) {
			// Sync via PWM duty cycle (for RING output on shared pin)
			hal_set_pwm_duty(level ? 255 : 0);
		}
		else {
			// Sync via direct GPIO write
			digitalWrite(state.syncOutputPin, level);
		}
		state.flags.syncTickPending = false;
	}
	// Run the current mode's behavior (melody, drums, live control, etc.)
	selectMode();
}


// ---------------------------------------------------------------------------
// 20 kHz TIMER0 ISR — the audio engine heart.
//
// Fires 20,000 times per second. Each interrupt:
//   1. Calls synthesizeVoices() if tones are enabled (or cue tone is active)
//   2. Increments sampleCounter (free-running time base for drums)
//   3. Increments periodTimer (measures sync pulse timing)
//
// synthesizeVoices() mixes all 4 voices, applies waveform shaping, and
// writes the result to PWM. This is where the actual sound is generated.
// ---------------------------------------------------------------------------
ISR(TIMER0_COMPA_vect) {
	if (state.flags.tonesEnabled || state.flags.cueToneActive) {
		synthesizeVoices();
	}
	state.sampleCounter++;
	state.periodTimer++;
}
