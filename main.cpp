/*
 * main.cpp - Native Microchip Studio (Atmel Studio) build entry point.
 *
 * The AdjacentReality sketch was written as a single Arduino translation unit:
 * all .cpp files share globals without headers.  This file replicates that
 * model (exactly what Visual Micro's __vm/.AdjacentReality.vsarduino.h did):
 *  1. include the Arduino-compat shim,
 *  2. include all header files for documentation,
 *  3. include the sketch .cpp files in the original order,
 *  4. provide main() that runs setup() once then loop() forever.
 */

#include "arduino.h"

/* ------------------------- header files --------------------------- */

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

/* ------------------------- sketch sources (original order) -------- */

#include "AdjacentReality.cpp"
#include "sequencer.cpp"
#include "drums.cpp"
#include "melody.cpp"
#include "synth.cpp"
#include "modes.cpp"
#include "ui.cpp"
#include "random.cpp"

/* ------------------------- entry point ---------------------------- */

int main() {
  setup();
  for (;;) {
    loop();
  }
  return 0;
}
