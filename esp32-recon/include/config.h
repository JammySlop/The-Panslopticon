// Scan tuning. Change behaviour here rather than in the scanner code.
#pragma once

#include <soc/soc_caps.h>
#include <stddef.h>
#include <stdint.h>

namespace config {

// True on chips with a 5 GHz radio (ESP32-C5), false on 2.4 GHz-only chips
// (ESP32-S3 on the Nano).
constexpr bool kDualBand =
#if SOC_WIFI_SUPPORT_5G
    true;
#else
    false;
#endif

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
// The scan visits every channel the regulatory domain allows: 13 on 2.4 GHz,
// plus ~25 more on 5 GHz for dual-band chips, so a full sweep takes roughly
// (channel count x dwell).
constexpr uint32_t kWifiDwellMsPerChannel = 360;

// Passive (false): only listen to advertisements; transmits nothing. Active
// (true): send a scan request to each device to get its scan response, where
// many devices put their name. Passive keeps the scanner receive-only at the
// cost of fewer names.
constexpr bool kBleActiveScan = false;
constexpr uint32_t kBleScanSeconds = 5;

constexpr uint32_t kPauseBetweenCyclesMs = 1000;

// --- Tier 2: monitor (promiscuous) mode ------------------------------------
// Receive-only capture of 802.11 frame headers. Set false to skip the monitor
// phase entirely and behave like the scan-only build.
constexpr bool kMonitorEnabled = true;

// Channels to hop through. 1/6/11 are the non-overlapping 2.4 GHz channels and
// carry most traffic; add 2-5,7-10,12-13 for completeness at the cost of time.
// On 5 GHz, 36-48 (UNII-1) and 149-165 (UNII-3) are the non-DFS channels most
// home and office APs use. DFS channels 52-144 can be added; listening on them
// is fine, it is only transmitting there that radar rules restrict.
constexpr uint8_t kMonitorChannels[] = {
    1, 6, 11,
#if SOC_WIFI_SUPPORT_5G
    36, 40, 44, 48, 149, 153, 157, 161, 165,
#endif
};
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
