// USB serial console for settings that are painful to enter on a resistive
// touch screen, mainly the dongle's WiFi password. Type "help" in a serial monitor.
#pragma once

namespace console {

// Call every loop; handles complete lines as they arrive.
void poll();

}  // namespace console
