#pragma once

#include <vector>

#include "scan_types.h"

namespace ble_recon {

void begin();

// Runs one blocking sweep. Results are sorted strongest first.
std::vector<BleDevice> scan();

}  // namespace ble_recon
