#ifndef STATE_H
#define STATE_H

#include <stdint.h>

// ---------------------------------------------------------------------------
// FirmwareState — all mutable firmware state in one struct.
//
// Large arrays (Chords, ChordsB, Scale, currentScale, Acc/Freq/Amp) remain
// as separate globals to keep the struct size manageable on ATtiny85 (512 B
// RAM).  Everything else lives here.
// ---------------------------------------------------------------------------

struct FirmwareState {

	// ---- sequencer ----
	volatile uint8_t  stepIndex;
	volatile uint16_t sampleCounter;
	volatile uint16_t periodTimer;          // shared with 20 kHz ISR
	uint16_t barCounter;
	uint16_t drumTimer;
	uint8_t  barLength;        // must be <= DEFAULT_BAR_LENGTH (32)
	int      tempoDivisor;
	uint32_t watchdogcounter;
	uint8_t  noisePatternIndex;

	// ---- LDR / sensor ----
	uint16_t rawLdr;
	int16_t  lightLevel;
	int16_t  scaledLight;
	int16_t  pitchBend;
	int16_t  prevLdr;       // previous lightLevel for delta
	int16_t  ldrDelta;      // current - previous (rate of change)
	int8_t   ldrVelocity;   // smoothed delta (positive = sweep up, neg = sweep down)

	// ---- music parameters ----
	uint8_t  scaleRoot;
	uint8_t  scaleType;
	int8_t   modulationInterval;
	uint8_t  transposeSteps;
	uint8_t  octaveOffset;
	uint8_t  distortionAmount;
	uint8_t  decayMode;
	uint8_t  noteDecay;
	uint8_t  playMode;
	int      octArray;

	// ---- drums ----
	uint8_t  drumParamA;
	uint8_t  drumParamB;
	uint8_t  drumParamC;
	uint8_t  drumPatternIndex;
	uint16_t drumDecayLength;
	uint8_t  drumMask;
	unsigned int BDseq;
	unsigned int SDseq;
	unsigned int HHseq;
	uint8_t  timbreKick;    // 0-2: kick waveform variant
	uint8_t  timbreSnare;   // 0-2: snare waveform variant
	uint8_t  timbreHihat;   // 0-2: hihat waveform variant

	// ---- sync ----
	uint8_t  syncOutputPin;
	uint16_t syncPeriod;

	// ---- voice allocation ----
	uint8_t  nextVoice;
	uint8_t  cueToneVoice;

	// ---- mode ----
	uint8_t  mode;

	// ---- bitfield flags ----
	struct {
		bool transitioning        : 1;
		bool recordMelodyNotBass  : 1;
		bool swingEnabled         : 1;
		bool pitchBendActive      : 1;
		bool cueToneActive        : 1;
		bool syncInputHigh        : 1;
		bool prevSyncInputHigh    : 1;
		bool syncTickPending      : 1;
		bool sendingSync          : 1;
		bool receivingSync        : 1;
		bool tonesEnabled         : 1;
		bool drumsEnabled         : 1;
		bool allowKickChange      : 1;
		bool allowSnareChange     : 1;
		bool button1              : 1;
		bool button2              : 1;
		bool prevButton1          : 1;
		bool prevButton2          : 1;
		bool playing              : 1;
		bool bothButtonsHeld      : 1;
		bool drumsMuted           : 1;
		bool modeFirstRun         : 1;
		bool recordingMelody      : 1;
		bool writeNotePending     : 1;
		bool eraseNotePending     : 1;
		bool preserveUserMelody   : 1;
		bool allowNoteEvolution   : 1;
		bool allowKeyTransposition: 1;
		bool button1Pressed       : 1;
		bool button1Released      : 1;
		bool button2Pressed       : 1;
		bool button2Released      : 1;
		bool writeBeatPending     : 1;
		bool eraseBeatPending     : 1;
		bool userCreatedBeat      : 1;
		bool bassEnabled          : 1;
		bool melodyEnabled        : 1;
		bool arpeggioEnabled      : 1;
		bool bassOctaveDown       : 1;
	} flags;
};

// Global state instance (defined in AdjacentReality.cpp)
extern struct FirmwareState state;

#endif
