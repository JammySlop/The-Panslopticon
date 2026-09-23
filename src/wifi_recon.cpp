#include "wifi_recon.h"

#include <Arduino.h>
#include <WiFi.h>

#include <algorithm>
#include <vector>

#include "config.h"

namespace wifi_recon {
namespace {

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

}  // namespace

void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();  // Scan only. This device never joins a network.
}

void scanAndReport() {
    const int16_t found = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true,
                                            config::kWifiPassiveScan,
                                            config::kWifiDwellMsPerChannel);
    if (found < 0) {
        Serial.printf("\nWiFi scan failed (%d)\n", found);
        return;
    }

    // Strongest (closest) first.
    std::vector<int> order(found);
    for (int i = 0; i < found; ++i) order[i] = i;
    std::sort(order.begin(), order.end(),
              [](int a, int b) { return WiFi.RSSI(a) > WiFi.RSSI(b); });

    Serial.printf("\n--- WiFi: %d network(s) ---\n", found);
    Serial.printf("%-32s  %-17s  %4s  %3s  %-11s  %s\n",
                  "SSID", "BSSID", "RSSI", "CH", "SECURITY", "FLAG");
    for (int i : order) {
        const String ssid = WiFi.SSID(i);
        const wifi_auth_mode_t auth = WiFi.encryptionType(i);
        Serial.printf("%-32.32s  %-17s  %4d  %3d  %-11s  %s\n",
                      ssid.isEmpty() ? "<hidden>" : ssid.c_str(),
                      WiFi.BSSIDstr(i).c_str(),
                      static_cast<int>(WiFi.RSSI(i)),
                      static_cast<int>(WiFi.channel(i)),
                      authModeName(auth), securityFlag(auth));
    }

    WiFi.scanDelete();
}

}  // namespace wifi_recon
