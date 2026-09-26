// Plain records produced by the scanners and consumed by the reporter.
#pragma once

#include <Arduino.h>
#include <esp_wifi_types.h>

#include <map>
#include <vector>

// --- Tier 1: fields the scans already receive ------------------------------

struct WifiNetwork {
    String ssid;  // Empty for hidden networks.
    String bssid;
    int rssi;
    int channel;  // 1-14 is 2.4 GHz, 36 and up is 5 GHz.
    wifi_auth_mode_t auth;

    const char* pairwiseCipher;  // "CCMP", "TKIP", ...
    const char* groupCipher;
    bool wps;                    // WPS enabled: a common weak point.
    String phy;                  // e.g. "b/g/n", "a/n/ac/ax".
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
    // Packets seen per channel. A map because 5 GHz channel numbers are sparse
    // (36, 40, ... 165); ordered so the report lists 2.4 GHz first.
    std::map<uint8_t, uint32_t> channelPackets;
};
