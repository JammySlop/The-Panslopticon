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

// --- Shared SPI bus (ESP32-C5 build only) ----------------------------------
// The display, touch controller and SD card all hang off the C5's default SPI
// pins, each with its own chip-select. Defined when any of them is compiled in.
#if RECON_DISPLAY || RECON_SD
constexpr int8_t kSpiSck = 10;   // TFT SCK / T_CLK / SD SCK
constexpr int8_t kSpiMosi = 8;   // TFT SDI / T_DIN / SD MOSI
constexpr int8_t kSpiMiso = 9;   // T_DO / SD MISO. Leave the TFT's SDO unconnected:
                                 // it doesn't release the line and corrupts touch reads.
#endif

// --- Touch display (ESP32-C5 build only) -----------------------------------
// 2.8"/3.2" 240x320 SPI TFT: ILI9341 panel + XPT2046 touch controller. Compiled
// in when platformio.ini defines RECON_DISPLAY. Wiring is in the README.
#if RECON_DISPLAY
constexpr int8_t kTftCs = 6;
constexpr int8_t kTftDc = 5;
constexpr int8_t kTftReset = 4;
constexpr int8_t kTftBacklight = 1;  // TFT LED
constexpr int8_t kTouchCs = 0;
constexpr int8_t kTouchIrq = 24;

// 10 MHz is dependable over breadboard jumpers; 40 MHz is only safe on short,
// soldered leads. A full-screen redraw takes ~125 ms at 10 MHz.
constexpr uint32_t kTftSpiHz = 10000000;
constexpr uint8_t kTftRotation = 1;       // Landscape, 320x240, pins on the left.
constexpr uint32_t kTouchPollMs = 30;
#endif

// --- SD card storage (ESP32-C5 build only) ---------------------------------
// 6-pin SPI microSD reader on the shared bus. Compiled in when platformio.ini
// defines RECON_SD. Every sweep's JSON line is appended to a per-boot file so
// the scanner keeps a local record with no computer attached.
#if RECON_SD
constexpr int8_t kSdCs = 23;              // The last free non-strapping pin (J3).
constexpr uint32_t kSdSpiHz = 10000000;   // Conservative for the shared bus.
constexpr char kSdDir[] = "/recon";       // Session files: /recon/scanNNNN.jsonl.
// Keeping the file open and flushing periodically is far faster than reopening
// per line. A power cut loses at most this much unflushed data.
constexpr uint32_t kSdFlushMs = 5000;
#endif

// --- WiFi export hotspot (ESP32-C5 build only) -----------------------------
// The ONE exception to receive-only: when an export is explicitly triggered the
// board runs its own password-protected access point, serves the SD files, then
// shuts the radio off and resumes passive scanning. It never joins a network
// and never transmits at any other time. Compiled in with RECON_WIFI_EXPORT
// (which needs RECON_SD).
#if RECON_WIFI_EXPORT
constexpr char kExportApPrefix[] = "recon-export-";  // Suffixed with a random tag.
constexpr uint8_t kExportApChannel = 6;
constexpr uint8_t kExportPassLen = 12;               // Random WPA2 password length.
// Tear the hotspot down after this long with no client connected, and after the
// hard cap no matter what, so the radio is never left transmitting.
constexpr uint32_t kExportIdleTimeoutMs = 120000;
constexpr uint32_t kExportMaxMs = 900000;
#endif

}  // namespace config
