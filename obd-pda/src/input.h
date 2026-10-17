// Touch panel taps, plus the board's BOOT button as a hardware back button.
#pragma once

#include <stdint.h>

namespace input {

struct Event {
    enum class Kind : uint8_t {
        None,
        Tap,        // at x, y in screen coordinates
        Back,       // BOOT pressed
        Calibrate,  // BOOT held for config::kBootCalibrateMs
    };
    Kind kind = Kind::None;
    int16_t x = 0;
    int16_t y = 0;
    // A finger still held down after config::kTouchRepeatDelayMs repeats its
    // tap; only scroll arrows act on repeats.
    bool repeat = false;
};

void begin();
// Call every loop. Returns at most one event per call.
Event poll();

// Applies the calibration saved in flash. False if there is none yet.
bool loadCalibration();
// Runs LovyanGFX's four-corner calibration (blocks until all four corners are
// tapped), then applies and saves it.
void calibrate(uint16_t fg, uint16_t bg);

// Prints every tap on the serial console (the "touch" command).
void setEcho(bool on);
bool echo();

}  // namespace input
