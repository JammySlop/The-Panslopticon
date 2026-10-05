// Three debounced buttons: UP, DOWN and SELECT (long press = BACK).
#pragma once

namespace buttons {

enum class Event : unsigned char { None, Up, Down, Select, Back };

void begin();
// Call every loop. Returns at most one event per call.
Event poll();

}  // namespace buttons
