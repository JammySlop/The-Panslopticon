// Board-level and timing configuration. Keep magic numbers here, not in main.
#pragma once

#include <stdint.h>

namespace config {

inline constexpr uint32_t kSerialBaud = 115200;

// The Nano ESP32 enumerates as USB CDC, so Serial is not ready the instant
// begin() returns. Wait briefly, but never block forever when running on
// battery with no host attached.
inline constexpr uint32_t kSerialReadyTimeoutMs = 2000;

inline constexpr uint32_t kHeartbeatIntervalMs = 500;
inline constexpr uint32_t kStatusIntervalMs = 5000;

}  // namespace config
