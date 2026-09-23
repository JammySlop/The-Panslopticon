#include "wifi_recon.h"

#include <WiFi.h>

#include <algorithm>

#include "config.h"

namespace wifi_recon {

const char* authModeName(wifi_auth_mode_t mode) {
    switch (mode) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
        case WIFI_AUTH_WPA3_PSK:        return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/WPA3";
        case WIFI_AUTH_WAPI_PSK:        return "WAPI";
        case WIFI_AUTH_WPA3_ENT_192:    return "WPA3-ENT192";
        default:                        return "?";
    }
}

// Marks networks whose security deserves attention in a survey. Returns a short
// label for the FLAG column, or "" when there is nothing to note.
const char* securityFlag(wifi_auth_mode_t mode) {
    // TODO(human)
    (void)mode;
    return "";
}

void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();  // Scan only. This device never joins a network.
}

std::vector<WifiNetwork> scan() {
    std::vector<WifiNetwork> networks;

    const int16_t found = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true,
                                            config::kWifiPassiveScan,
                                            config::kWifiDwellMsPerChannel);
    if (found < 0) {
        // Plain text, not JSON, so the web page ignores it.
        Serial.printf("WiFi scan failed (%d)\n", found);
        return networks;
    }

    networks.reserve(found);
    for (int i = 0; i < found; ++i) {
        networks.push_back({WiFi.SSID(i), WiFi.BSSIDstr(i),
                            static_cast<int>(WiFi.RSSI(i)),
                            static_cast<int>(WiFi.channel(i)),
                            WiFi.encryptionType(i)});
    }
    WiFi.scanDelete();

    std::sort(networks.begin(), networks.end(),
              [](const WifiNetwork& a, const WifiNetwork& b) { return a.rssi > b.rssi; });
    return networks;
}

}  // namespace wifi_recon
