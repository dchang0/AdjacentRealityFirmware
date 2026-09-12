#ifndef HAL_H
#define HAL_H

#include <avr/io.h>
#include <avr/interrupt.h>
#include "config.h"

// ---------------------------------------------------------------------------
// Hardware Abstraction Layer for ATtiny85
//
// All functions are static inline to guarantee zero overhead — the compiler
// collapses them to direct register writes, identical to the original code.
// ---------------------------------------------------------------------------

// ==========================================================================
// CLOCK SPEED SELECTION
// ==========================================================================
//
// The ATtiny85 can run at two speeds. You must change code in TWO places
// depending on which clock you want:
//
//   16 MHz (PLL)                          8 MHz (internal RC)
//   ─────────────                         ──────────────────
//   hal_init_pll() IS called              hal_init_pll() is NOT called
//   in setup() — see AdjacentReality.cpp   in setup() — skip or remove it
//   (uncomment the line above timer0)     (leave commented out)
//
//   hal_init_timer0(TIMER0_COMPARE_16MHZ) hal_init_timer0(TIMER0_COMPARE_8MHZ)
//   passes 99                             passes 49
//
// Both yield a 20 kHz audio sample rate:
//   16 MHz / 8 (prescale) / 100 = 20 kHz   (OCR0A = 99, so divisor = 99+1)
//    8 MHz / 8 (prescale) /  50 = 20 kHz   (OCR0A = 49, so divisor = 49+1)
//
// The constants TIMER0_COMPARE_16MHZ and TIMER0_COMPARE_8MHZ are defined
// in config.h.
// ==========================================================================

// --- PLL (16 MHz clock source) ---------------------------------------------
// For 16 MHz: call hal_init_pll() in setup() BEFORE hal_init_timer0().
// For  8 MHz: do NOT call this function — the ATtiny85 defaults to 8 MHz.

static inline void hal_init_pll(void) {
  PLLCSR = (1 << PCKE) | (1 << PLLE);
}

// --- Timer0: 20 kHz sample-rate ISR ----------------------------------------
// The compare value determines the sample rate. Pass the correct constant
// for your clock speed (see clock speed note above).

static inline void hal_init_timer0(uint8_t compare) {
  TIMSK = 0;                                  // all timer interrupts off
  TCCR0B = (1 << WGM02) | (2 << CS00);       // CTC mode, /8 prescale
  OCR0A = compare;                            // 99 for 16 MHz, 49 for 8 MHz
  TIMSK = (1 << OCIE0A);                      // enable compare-match A ISR
}

// --- Timer1: PWM tone output on OC1B (PB4) ---------------------------------

static inline void hal_init_timer1(void) {
  TCCR1 = (1 << CS10);                        // no prescale
  GTCCR = (1 << PWM1B) | (2 << COM1B0);      // PWM B mode, clear on match
}

// --- PWM duty-cycle (OCR1B) ------------------------------------------------

static inline void hal_set_pwm_duty(uint8_t duty) {
  OCR1B = duty;
}

// --- Masked PORTB write (drums / sync GPIO) --------------------------------
// Only the bits set in `mask` are modified; other PORTB bits are preserved.

static inline void hal_drum_write(uint8_t mask, uint8_t value) {
  PORTB = (PORTB & ~mask) | (value & mask);
}

// --- Direct PORTB read -----------------------------------------------------

static inline uint8_t hal_read_portb(void) {
  return PINB;
}

// --- Interrupt control -----------------------------------------------------

static inline void hal_enable_interrupts(void) {
  sei();
}

// --- Watchdog timer: ~62.5 Hz interrupt (16ms period) -----------------------
// WDP[2:0]=000 = 16ms timeout. Actual step rate controlled by tempoDivisor
// in the ISR (tempoDivisor 8-22 → ~2.8-7.8 steps/sec).

static inline void hal_init_watchdog(void) {
  WDTCR = (1 << WDIE) | (0 << WDP0);         // 16ms period (~62.5 Hz)
}

#endif /* HAL_H */
