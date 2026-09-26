// On-device dashboard for a 320x240 ILI9341 touch display. Tap anywhere to
// change page. Builds without RECON_DISPLAY get no-op stubs.
#pragma once

#include <vector>

#include "scan_types.h"

namespace display {

#if RECON_DISPLAY
// Starts the display task. Rendering and touch run there, so the screen stays
// responsive while the main loop is blocked in a scan.
void begin();

// Hand the latest results to the display. Cheap: copies under a lock and
// returns; the display task redraws on its own schedule.
void wifi(uint32_t cycle, const std::vector<WifiNetwork>& networks);
void ble(uint32_t cycle, const std::vector<BleDevice>& devices);
void monitor(uint32_t cycle, const MonitorReport& report);
#else
inline void begin() {}
inline void wifi(uint32_t, const std::vector<WifiNetwork>&) {}
inline void ble(uint32_t, const std::vector<BleDevice>&) {}
inline void monitor(uint32_t, const MonitorReport&) {}
#endif

}  // namespace display
