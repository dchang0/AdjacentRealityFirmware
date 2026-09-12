#ifndef MELODY_H
#define MELODY_H

#include <avr/pgmspace.h>

// Scale tables (stored in flash)
extern const uint16_t PROGMEM Scale[38];

// Current scale data (flash)
extern const byte currentScale[15];

// Chord/melody/bass sequences
extern unsigned long Chords[];
extern unsigned int ChordsB[];

// Chord/melody/bass generation
void clearMelodyPattern();
void removeNoteFromStep(byte steppo);
void generateChordPattern();
void generateMelodyPattern();
void generateBassPattern();
void clearBassPattern();

// Cue tone
void playCueTone(uint16_t freq);

#endif
