// User settings, persisted in NVS. Changed from the Settings screen or over
// the USB serial console (see console.h).
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace settings {

enum class LinkKind : uint8_t {
    Elm327Wifi = 0,  // a WiFi ELM327 dongle in the OBD port
    DirectCan = 1,   // our own CAN transceiver wired to the OBD port
};

struct Settings {
    LinkKind link;
    uint32_t canBitrate;  // direct CAN only; 0 = auto-detect
    char wifiSsid[33];
    char wifiPass[65];
    // Empty: use the gateway address the dongle's DHCP hands out, which is the
    // dongle itself. Set it only if your dongle does something unusual.
    char elmHost[16];
    uint16_t elmPort;  // 0 = try the usual ports (35000, then 23)
    // SD card logging.
    bool recordSniff;  // sniffer frames to sniff.log while the sniffer is open
    bool recordLive;   // live-data samples to live.csv while live data is open
    bool saveScans;    // a report file for every trouble-code read
};

void load();
void save();
Settings& get();

}  // namespace settings
