#ifndef DRUMS_H
#define DRUMS_H

#include "state.h"

// Pattern generation
void clearDrumPatterns();
void generateKickPattern();
void generateSnarePattern();
void generateHiHatPattern();
void regenerateAllDrums();

// Drum output
void playDrums();
void playDrumSample(int i, byte selector);
byte getDrumSample(long i, byte selector);

#endif
