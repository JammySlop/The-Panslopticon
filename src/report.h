// Prints scan results over serial, either as human-readable tables or as one
// JSON object per line for the web interface (see config::kJsonOutput).
#pragma once

#include <stdint.h>

#include <vector>

#include "scan_types.h"

namespace report {

void cycleStart(uint32_t cycle);
void wifi(uint32_t cycle, const std::vector<WifiNetwork>& networks);
void ble(uint32_t cycle, const std::vector<BleDevice>& devices);

}  // namespace report
