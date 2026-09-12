#ifndef SEQUENCER_H
#define SEQUENCER_H

#include "state.h"

// Sequencer functions (called from watchdog ISR)
void playSequencerStep();
void processStep();

#endif
