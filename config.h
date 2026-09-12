#ifndef CONFIG_H
#define CONFIG_H

// Firmware seed for random number generator
#define FIRMWARE_SEED 10023

// Sequencer defaults
#define DEFAULT_BAR_LENGTH 32
#define NUM_VOICES 4
#define NUM_MODES 8

// Drum/sync GPIO mask
#define DRUM_SYNC_MASK B00000010

// PWM midpoint for tone output
#define PWM_MIDPOINT 128

// Timer0 compare values — pick the one matching your clock speed (see hal.h)
// Formula: sample_rate = clock / 8 / (OCR0A + 1)
//   16 MHz PLL:  16000000 / 8 / 100 = 20 kHz  -> TIMER0_COMPARE_16MHZ (99)
//    8 MHz RC:    8000000 / 8 /  50 = 20 kHz  -> TIMER0_COMPARE_8MHZ  (49)
#define TIMER0_COMPARE_16MHZ 99
#define TIMER0_COMPARE_8MHZ 49

// Sync detection thresholds
#define SYNC_LOCK_COUNT 8
#define SYNC_TIMEOUT 1000
#define BUTTON_STUCK_TIMEOUT 400

// LDR thresholds
#define LDR_OFFSET 400
#define LDR_OCTAVE_THRESHOLD 1023
#define LDR_OCTAVE_OFFSET 384
#define KICK_LIGHT_MAX 300
#define HAT_LIGHT_MAX 400

// Drum waveform noise offset (100000 truncated to 16-bit signed int)
#define SD_NOISE_OFFSET (-31072)

// Cue tone parameters
#define CUE_TONE_OCTAVE 1
// Amp = (1+CUE_TONE_DISTORTION)<<10. Value 1 = 2048 = ~102ms decay at 20kHz.
#define CUE_TONE_DISTORTION 1

// Per-mode cue tone Scale indices (chromatic, fourths spacing)
// playCueTone reads pgm_read_word(&Scale[index]) for the oscillator increment
static const uint8_t cueToneScaleIndex[8] PROGMEM = {
	18, // (mode 7) D5   — two fourths below group
	28, // (mode 0) C6   }
	23, // (mode 1) G5   — fourth below group
	29, // (mode 2) C#6  }
	30, // (mode 3) D6   } grouped
	31, // (mode 4) D#6  }
	32, // (mode 5) E6   }
	37  // (mode 6) A6   — major 6th above group
};

// Note trigger defaults
#define NOTE_TRIGGER_DISTORTION 10

// Mode motif patterns: [on_steps, off_steps, on_steps, off_steps, ..., 0]
// Short (dot) = 1 step ON, Long (dash) = 3 steps ON, gap = 1 step OFF
//
// Mode 0,1,5 Autotune:       · ·    (heartbeat)
// Mode 2      Record Melody:   · —    (short-long)
// Mode 3      Record Bass:     — ·    (long-short)
// Mode 4      Record Beat:     — —    (stomp)
// Mode 6      Button Solos:    · · ·  (triplet)
// Mode 7      Noise Generator: — · —  (syncopated)

// Autotune: · ·
// Record Melody: · —
// Record Bass: — ·
// Record Beat: — —
// Button Solos: · · ·
// Noise Generator: — · —

#endif
