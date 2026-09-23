// Scan tuning. Change behaviour here rather than in the scanner code.
#pragma once

#include <stdint.h>

#include <array>

namespace config {

constexpr uint32_t kSerialBaud = 115200;
// USB CDC is not ready the instant Serial.begin() returns. Wait briefly, but
// keep scanning even if no computer is attached.
constexpr uint32_t kSerialReadyTimeoutMs = 2000;

// true: one JSON object per line, for web/index.html.
// false: human-readable tables, for `pio device monitor`.
constexpr bool kJsonOutput = true;

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

// --- Tier 2: monitor (promiscuous) mode ------------------------------------
// Receive-only capture of 802.11 frame headers. Set false to skip the monitor
// phase entirely and behave like the scan-only build.
constexpr bool kMonitorEnabled = true;

// Channels to hop through. 1/6/11 are the non-overlapping 2.4 GHz channels and
// carry most traffic; add 2-5,7-10,12-13 for completeness at the cost of time.
constexpr std::array<uint8_t, 3> kMonitorChannels = {1, 6, 11};
// Time spent listening on each channel. Longer catches more, sweeps slower.
constexpr uint32_t kMonitorDwellMs = 400;

// Bounded so a crowded environment cannot exhaust RAM.
constexpr uint32_t kMonitorQueueLen = 256;
constexpr size_t kMaxMonitorClients = 80;
constexpr size_t kMaxMonitorAps = 50;
constexpr size_t kMaxClientsPerAp = 40;
constexpr size_t kMaxProbesPerClient = 6;

// Deauth/disassoc frames per BSSID within one sweep before it is flagged as a
// likely attack. Normal roaming produces only a few.
constexpr uint32_t kDeauthAlertThreshold = 8;

}  // namespace config
