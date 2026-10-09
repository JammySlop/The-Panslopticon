// 5-way navigation switch (plus the board's BOOT button as an extra centre).
// Directions fire on press and auto-repeat while held; the centre fires
// Select on release, or Back if held.
#pragma once

#include <stdint.h>

namespace buttons {

enum class Event : uint8_t { None, Up, Down, Left, Right, Select, Back };

void begin();
// Call every loop. Returns at most one event per call.
Event poll();

// Ladder wiring only: the last ADC reading, for checking the thresholds.
uint16_t ladderMillivolts();
// Name of the key currently held, or "none" (for the console).
const char* heldKeyName();

}  // namespace buttons
