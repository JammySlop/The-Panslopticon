#include "report.h"

#include <Arduino.h>

#include "config.h"
#include "wifi_recon.h"

namespace report {
namespace {

// SSIDs and BLE names are arbitrary bytes chosen by whoever is broadcasting.
// Escape quotes, backslashes and control characters so a hostile name cannot
// break the JSON line.
void appendJsonString(String& out, const String& s) {
    out += '"';
    for (size_t i = 0; i < s.length(); ++i) {
        const char c = s[i];
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (static_cast<unsigned char>(c) < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", static_cast<unsigned>(c));
                    out += buf;
                } else {
                    out += c;
                }
        }
    }
    out += '"';
}

void appendHeader(String& out, const char* type, uint32_t cycle) {
    out += "{\"t\":\"";
    out += type;
    out += "\",\"cycle\":";
    out += cycle;
    out += ",\"up\":";
    out += millis() / 1000;
    out += ",\"items\":[";
}

void printWifiJson(uint32_t cycle, const std::vector<WifiNetwork>& networks) {
    String out;
    out.reserve(64 + networks.size() * 128);
    appendHeader(out, "wifi", cycle);
    for (size_t i = 0; i < networks.size(); ++i) {
        const WifiNetwork& n = networks[i];
        if (i > 0) out += ',';
        out += "{\"ssid\":";
        appendJsonString(out, n.ssid);
        out += ",\"bssid\":";
        appendJsonString(out, n.bssid);
        out += ",\"rssi\":";
        out += n.rssi;
        out += ",\"ch\":";
        out += n.channel;
        out += ",\"auth\":";
        appendJsonString(out, wifi_recon::authModeName(n.auth));
        out += ",\"flag\":";
        appendJsonString(out, wifi_recon::securityFlag(n.auth));
        out += '}';
    }
    out += "]}";
    Serial.println(out);
}

void printBleJson(uint32_t cycle, const std::vector<BleDevice>& devices) {
    String out;
    out.reserve(64 + devices.size() * 160);
    appendHeader(out, "ble", cycle);
    for (size_t i = 0; i < devices.size(); ++i) {
        const BleDevice& d = devices[i];
        if (i > 0) out += ',';
        out += "{\"addr\":";
        appendJsonString(out, d.address);
        out += ",\"type\":";
        appendJsonString(out, d.addressType);
        out += ",\"rssi\":";
        out += d.rssi;
        out += ",\"name\":";
        appendJsonString(out, d.name);
        out += ",\"mfg\":";
        appendJsonString(out, d.manufacturer);
        out += ",\"tx\":";
        if (d.hasTxPower) {
            out += d.txPower;
        } else {
            out += "null";
        }
        out += ",\"svc\":[";
        for (size_t j = 0; j < d.services.size(); ++j) {
            if (j > 0) out += ',';
            appendJsonString(out, d.services[j]);
        }
        out += "]}";
    }
    out += "]}";
    Serial.println(out);
}

void printWifiTable(const std::vector<WifiNetwork>& networks) {
    Serial.printf("\n--- WiFi: %u network(s) ---\n",
                  static_cast<unsigned>(networks.size()));
    Serial.printf("%-32s  %-17s  %4s  %3s  %-11s  %s\n",
                  "SSID", "BSSID", "RSSI", "CH", "SECURITY", "FLAG");
    for (const WifiNetwork& n : networks) {
        Serial.printf("%-32.32s  %-17s  %4d  %3d  %-11s  %s\n",
                      n.ssid.isEmpty() ? "<hidden>" : n.ssid.c_str(),
                      n.bssid.c_str(), n.rssi, n.channel,
                      wifi_recon::authModeName(n.auth),
                      wifi_recon::securityFlag(n.auth));
    }
}

void printBleTable(const std::vector<BleDevice>& devices) {
    Serial.printf("\n--- BLE: %u device(s) in %lus ---\n",
                  static_cast<unsigned>(devices.size()),
                  static_cast<unsigned long>(config::kBleScanSeconds));
    Serial.printf("%-17s  %-7s  %4s  %-20s  %s\n",
                  "ADDRESS", "TYPE", "RSSI", "NAME", "DETAILS");
    for (const BleDevice& d : devices) {
        String details;
        if (!d.manufacturer.isEmpty()) details += "mfg=" + d.manufacturer + " ";
        if (d.hasTxPower) details += "tx=" + String(d.txPower) + "dBm ";
        for (const String& svc : d.services) details += "svc=" + svc + " ";

        Serial.printf("%-17s  %-7s  %4d  %-20.20s  %s\n",
                      d.address.c_str(), d.addressType, d.rssi,
                      d.name.c_str(), details.c_str());
    }
}

}  // namespace

void cycleStart(uint32_t cycle) {
    if (config::kJsonOutput) return;
    Serial.printf("\n=========== scan #%lu  (uptime %lus) ===========\n",
                  static_cast<unsigned long>(cycle),
                  static_cast<unsigned long>(millis() / 1000));
}

void wifi(uint32_t cycle, const std::vector<WifiNetwork>& networks) {
    if (config::kJsonOutput) {
        printWifiJson(cycle, networks);
    } else {
        printWifiTable(networks);
    }
}

void ble(uint32_t cycle, const std::vector<BleDevice>& devices) {
    if (config::kJsonOutput) {
        printBleJson(cycle, devices);
    } else {
        printBleTable(devices);
    }
}

}  // namespace report
