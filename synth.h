#ifndef SYNTH_H
#define SYNTH_H

#include "state.h"

// Voice arrays (shared with ISR)
extern volatile unsigned int Acc[];
extern volatile unsigned int Freq[];
extern volatile unsigned int Amp[];

// Polyphonic synthesis (called from 20 kHz ISR)
void synthesizeVoices();

// Single note trigger
void triggerNote(int Hz, int oct, int distortion);

#endif
