#include "wifi_recon.h"

#include <WiFi.h>
#include <esp_wifi.h>

#include <algorithm>

#include "config.h"
#include "oui.h"

namespace wifi_recon {

const char* authModeName(wifi_auth_mode_t mode) {
    switch (mode) {
        case WIFI_AUTH_OPEN:            return "OPEN";
        case WIFI_AUTH_WEP:             return "WEP";
        case WIFI_AUTH_WPA_PSK:         return "WPA";
        case WIFI_AUTH_WPA2_PSK:        return "WPA2";
        case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/WPA2";
        case WIFI_AUTH_WPA_ENTERPRISE:  return "WPA-ENT";
        case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ENT";
        case WIFI_AUTH_WPA3_PSK:
        case WIFI_AUTH_WPA3_EXT_PSK:    return "WPA3";
        case WIFI_AUTH_WPA2_WPA3_PSK:
        case WIFI_AUTH_WPA3_EXT_PSK_MIXED_MODE: return "WPA2/WPA3";
        case WIFI_AUTH_WPA3_ENTERPRISE: return "WPA3-ENT";
        case WIFI_AUTH_WPA2_WPA3_ENTERPRISE: return "WPA2/WPA3-ENT";
        case WIFI_AUTH_WPA3_ENT_192:    return "WPA3-ENT192";
        case WIFI_AUTH_OWE:             return "OWE";  // Encrypted, but no password.
        case WIFI_AUTH_DPP:             return "DPP";
        case WIFI_AUTH_WAPI_PSK:        return "WAPI";
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

namespace {

const char* cipherName(wifi_cipher_type_t c) {
    switch (c) {
        case WIFI_CIPHER_TYPE_NONE:      return "none";
        case WIFI_CIPHER_TYPE_WEP40:     return "WEP40";
        case WIFI_CIPHER_TYPE_WEP104:    return "WEP104";
        case WIFI_CIPHER_TYPE_TKIP:      return "TKIP";
        case WIFI_CIPHER_TYPE_CCMP:      return "CCMP";
        case WIFI_CIPHER_TYPE_TKIP_CCMP: return "TKIP+CCMP";
        case WIFI_CIPHER_TYPE_GCMP:      return "GCMP";
        case WIFI_CIPHER_TYPE_GCMP256:   return "GCMP256";
        default:                         return "";
    }
}

// Fills the Tier 1 fields that WiFi.SSID()/RSSI()/etc. don't expose, by reading
// the raw scan record the driver kept.
void addDetails(int index, WifiNetwork& n) {
    const auto* rec =
        reinterpret_cast<const wifi_ap_record_t*>(WiFi.getScanInfoByIndex(index));
    if (rec == nullptr) return;

    n.pairwiseCipher = cipherName(rec->pairwise_cipher);
    n.groupCipher = cipherName(rec->group_cipher);
    n.wps = rec->wps;

    // Slash-separated because the 5 GHz modes ("ac", "ax") are two letters.
    String phy;
    auto add = [&phy](bool on, const char* mode) {
        if (!on) return;
        if (!phy.isEmpty()) phy += '/';
        phy += mode;
    };
    add(rec->phy_11a, "a");
    add(rec->phy_11b, "b");
    add(rec->phy_11g, "g");
    add(rec->phy_11n, "n");
    add(rec->phy_11ac, "ac");
    add(rec->phy_11ax, "ax");
    if (rec->phy_lr) phy += "+lr";
    n.phy = phy;

    if (rec->country.cc[0]) {
        char cc[3] = {rec->country.cc[0], rec->country.cc[1], 0};
        n.country = cc;
    }
    n.vendor = oui::vendor(rec->bssid);
}

}  // namespace

void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();  // Scan only. This device never joins a network.
#if SOC_WIFI_SUPPORT_5G
    // Scans and monitor-mode channel hops cover both bands. Without this the
    // driver may stay on 2.4 GHz and reject 5 GHz channels.
    WiFi.setBandMode(WIFI_BAND_MODE_AUTO);
#endif
}

std::vector<WifiNetwork> scan() {
    std::vector<WifiNetwork> networks;

    const int16_t found = WiFi.scanNetworks(/*async=*/false, /*show_hidden=*/true,
                                            config::kWifiPassiveScan,
                                            config::kWifiDwellMsPerChannel);
    if (found < 0) {
        Serial.printf("WiFi scan failed (%d)\n", found);
        return networks;
    }

    networks.reserve(found);
    for (int i = 0; i < found; ++i) {
        WifiNetwork n{};
        n.ssid = WiFi.SSID(i);
        n.bssid = WiFi.BSSIDstr(i);
        n.rssi = WiFi.RSSI(i);
        n.channel = WiFi.channel(i);
        n.auth = WiFi.encryptionType(i);
        n.pairwiseCipher = "";
        n.groupCipher = "";
        n.wps = false;
        addDetails(i, n);
        networks.push_back(std::move(n));
    }
    WiFi.scanDelete();

    std::sort(networks.begin(), networks.end(),
              [](const WifiNetwork& a, const WifiNetwork& b) { return a.rssi > b.rssi; });
    return networks;
}

}  // namespace wifi_recon
