// Plain records produced by the scanners and consumed by the reporter.
#pragma once

#include <Arduino.h>
#include <esp_wifi_types.h>

#include <vector>

struct WifiNetwork {
    String ssid;  // Empty for hidden networks.
    String bssid;
    int rssi;
    int channel;
    wifi_auth_mode_t auth;
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
};
