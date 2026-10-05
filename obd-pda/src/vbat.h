// Vehicle battery voltage from the OBD port's pin 16, via a resistor divider.
#pragma once

namespace vbat {

void begin();
// Call every loop; samples at config::kVbatSampleMs.
void update();
// Smoothed volts at the OBD port. 0 when running from USB only.
float volts();

}  // namespace vbat
