// Scan tuning. Change behaviour here rather than in the scanner code.
#pragma once

#include <stdint.h>

namespace config {

constexpr uint32_t kSerialBaud = 115200;
// USB CDC is not ready the instant Serial.begin() returns. Wait briefly, but
// keep scanning even if no computer is attached.
constexpr uint32_t kSerialReadyTimeoutMs = 2000;

// Passive: only listen for beacons, transmit nothing. Active: send probe
// requests, which is faster but announces this device to every AP in range.
constexpr bool kWifiPassiveScan = true;
// Beacons arrive every ~102 ms, so dwell long enough to catch a few per channel.
constexpr uint32_t kWifiDwellMsPerChannel = 360;

// Active BLE scanning requests the scan response, which is where many devices
// put their name. Passive scanning sees fewer names.
constexpr bool kBleActiveScan = true;
constexpr uint32_t kBleScanSeconds = 5;

constexpr uint32_t kPauseBetweenCyclesMs = 1000;

}  // namespace config
