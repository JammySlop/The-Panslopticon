// Plain records produced by the scanners and consumed by the reporter.
#pragma once

#include <Arduino.h>
#include <esp_wifi_types.h>

#include <vector>

// --- Tier 1: fields the scans already receive ------------------------------

struct WifiNetwork {
    String ssid;  // Empty for hidden networks.
    String bssid;
    int rssi;
    int channel;
    wifi_auth_mode_t auth;

    const char* pairwiseCipher;  // "CCMP", "TKIP", ...
    const char* groupCipher;
    bool wps;                    // WPS enabled: a common weak point.
    String phy;                  // e.g. "bgn", "n", "ax".
    String country;              // Advertised regulatory domain, or "".
    String vendor;               // From the BSSID OUI, or "".
};

struct BleDevice {
    String address;
    const char* addressType;
    int rssi;
    String name;          // Empty when the device did not advertise one.
    String manufacturer;  // Company name, "0x1234" if unknown, or empty.
    bool hasTxPower;
    int txPower;
    std::vector<String> services;

    String appearance;    // "Watch", "Keyboard", ... or "".
    String product;       // Decoded from payload: "AirPods", "AirTag", ...
    bool hasDistance;
    float distanceM;      // Rough estimate from TX power and RSSI.
};

// --- Tier 2: monitor mode (promiscuous, receive-only) ----------------------

// A device seen transmitting probe requests, i.e. looking for networks. It is
// not currently associated with any AP in range.
struct ProbingClient {
    String mac;
    String vendor;
    bool randomized;
    int rssi;
    uint32_t frames;
    std::vector<String> probedSsids;  // Networks it asked for by name.
};

// An access point observed carrying traffic, with the count of distinct client
// MACs seen talking to it.
struct ApTraffic {
    String bssid;
    String ssid;      // Revealed name, if a beacon/probe-response carried one.
    String vendor;
    int clientCount;
    uint32_t frames;
};

// A burst of deauth/disassoc frames: the signature of a nearby WiFi attack.
struct SecurityAlert {
    String bssid;
    const char* kind;  // "deauth-flood".
    uint32_t count;
};

struct MonitorReport {
    std::vector<ProbingClient> clients;
    std::vector<ApTraffic> aps;
    std::vector<SecurityAlert> alerts;
    uint32_t channelPackets[14];  // Index 1..13; packets seen per channel.
};
