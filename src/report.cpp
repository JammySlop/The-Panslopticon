#include "report.h"

#include <Arduino.h>

#include "config.h"
#include "wifi_recon.h"

namespace report {
namespace {

// Emits `,"key":` before a value, or `"key":` for the first field in an object.
// Callers pass first=true once, then false, so objects stay comma-correct as
// fields are added or removed.
void keyPrefix(String& out, bool& first, const char* key) {
    out += first ? "\"" : ",\"";
    out += key;
    out += "\":";
    first = false;
}

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

void kvStr(String& out, bool& first, const char* key, const String& value) {
    keyPrefix(out, first, key);
    appendJsonString(out, value);
}

void kvInt(String& out, bool& first, const char* key, long value) {
    keyPrefix(out, first, key);
    out += value;
}

void kvBool(String& out, bool& first, const char* key, bool value) {
    keyPrefix(out, first, key);
    out += value ? "true" : "false";
}

// Appends `,"key":[ "a","b" ]` (a JSON array of strings).
void kvStrArray(String& out, bool& first, const char* key,
                const std::vector<String>& values) {
    keyPrefix(out, first, key);
    out += '[';
    for (size_t i = 0; i < values.size(); ++i) {
        if (i > 0) out += ',';
        appendJsonString(out, values[i]);
    }
    out += ']';
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
    out.reserve(64 + networks.size() * 192);
    appendHeader(out, "wifi", cycle);
    for (size_t i = 0; i < networks.size(); ++i) {
        const WifiNetwork& n = networks[i];
        if (i > 0) out += ',';
        out += '{';
        bool f = true;
        kvStr(out, f, "ssid", n.ssid);
        kvStr(out, f, "bssid", n.bssid);
        kvInt(out, f, "rssi", n.rssi);
        kvInt(out, f, "ch", n.channel);
        kvStr(out, f, "auth", wifi_recon::authModeName(n.auth));
        kvStr(out, f, "flag", wifi_recon::securityFlag(n.auth));
        kvStr(out, f, "cipher", n.pairwiseCipher);
        kvBool(out, f, "wps", n.wps);
        kvStr(out, f, "phy", n.phy);
        kvStr(out, f, "country", n.country);
        kvStr(out, f, "vendor", n.vendor);
        out += '}';
    }
    out += "]}";
    Serial.println(out);
}

void printBleJson(uint32_t cycle, const std::vector<BleDevice>& devices) {
    String out;
    out.reserve(64 + devices.size() * 200);
    appendHeader(out, "ble", cycle);
    for (size_t i = 0; i < devices.size(); ++i) {
        const BleDevice& d = devices[i];
        if (i > 0) out += ',';
        out += '{';
        bool f = true;
        kvStr(out, f, "addr", d.address);
        kvStr(out, f, "type", d.addressType);
        kvInt(out, f, "rssi", d.rssi);
        kvStr(out, f, "name", d.name);
        kvStr(out, f, "mfg", d.manufacturer);
        kvStr(out, f, "product", d.product);
        kvStr(out, f, "appearance", d.appearance);
        if (d.hasTxPower) {
            kvInt(out, f, "tx", d.txPower);
        } else {
            keyPrefix(out, f, "tx");
            out += "null";
        }
        if (d.hasDistance) {
            keyPrefix(out, f, "dist");
            out += String(d.distanceM, 1);
        }
        kvStrArray(out, f, "svc", d.services);
        out += '}';
    }
    out += "]}";
    Serial.println(out);
}

void printMonitorJson(uint32_t cycle, const MonitorReport& mon) {
    String out;
    out.reserve(256 + mon.clients.size() * 96 + mon.aps.size() * 80);
    out += "{\"t\":\"monitor\",\"cycle\":";
    out += cycle;
    out += ",\"up\":";
    out += millis() / 1000;

    out += ",\"clients\":[";
    for (size_t i = 0; i < mon.clients.size(); ++i) {
        const ProbingClient& c = mon.clients[i];
        if (i > 0) out += ',';
        out += '{';
        bool f = true;
        kvStr(out, f, "mac", c.mac);
        kvStr(out, f, "vendor", c.vendor);
        kvBool(out, f, "rand", c.randomized);
        kvInt(out, f, "rssi", c.rssi);
        kvInt(out, f, "frames", c.frames);
        kvStrArray(out, f, "probes", c.probedSsids);
        out += '}';
    }

    out += "],\"aps\":[";
    for (size_t i = 0; i < mon.aps.size(); ++i) {
        const ApTraffic& a = mon.aps[i];
        if (i > 0) out += ',';
        out += '{';
        bool f = true;
        kvStr(out, f, "bssid", a.bssid);
        kvStr(out, f, "ssid", a.ssid);
        kvStr(out, f, "vendor", a.vendor);
        kvInt(out, f, "clients", a.clientCount);
        kvInt(out, f, "frames", a.frames);
        out += '}';
    }

    out += "],\"alerts\":[";
    for (size_t i = 0; i < mon.alerts.size(); ++i) {
        const SecurityAlert& s = mon.alerts[i];
        if (i > 0) out += ',';
        out += '{';
        bool f = true;
        kvStr(out, f, "bssid", s.bssid);
        kvStr(out, f, "kind", s.kind);
        kvInt(out, f, "count", s.count);
        out += '}';
    }

    out += "],\"channels\":[";
    bool firstCh = true;
    for (int ch = 1; ch <= 13; ++ch) {
        if (mon.channelPackets[ch] == 0) continue;
        if (!firstCh) out += ',';
        firstCh = false;
        out += "{\"ch\":";
        out += ch;
        out += ",\"pkts\":";
        out += mon.channelPackets[ch];
        out += '}';
    }
    out += "]}";
    Serial.println(out);
}

void printMonitorTable(const MonitorReport& mon) {
    Serial.printf("\n--- Monitor: %u probing client(s), %u active AP(s) ---\n",
                  static_cast<unsigned>(mon.clients.size()),
                  static_cast<unsigned>(mon.aps.size()));
    for (const ProbingClient& c : mon.clients) {
        Serial.printf("  client %s  %4d dBm  %-14s  frames=%lu  probes=",
                      c.mac.c_str(), c.rssi,
                      c.randomized ? "(randomized)"
                                   : (c.vendor.isEmpty() ? "?" : c.vendor.c_str()),
                      static_cast<unsigned long>(c.frames));
        for (const String& s : c.probedSsids) Serial.printf("%s ", s.c_str());
        Serial.println();
    }
    for (const ApTraffic& a : mon.aps) {
        Serial.printf("  ap     %s  %-24.24s  clients=%d  frames=%lu\n",
                      a.bssid.c_str(), a.ssid.isEmpty() ? "?" : a.ssid.c_str(),
                      a.clientCount, static_cast<unsigned long>(a.frames));
    }
    for (const SecurityAlert& s : mon.alerts) {
        Serial.printf("  ALERT  %s  %s x%lu\n", s.bssid.c_str(), s.kind,
                      static_cast<unsigned long>(s.count));
    }
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
        if (!d.product.isEmpty()) details += "product=" + d.product + " ";
        if (!d.appearance.isEmpty()) details += "is=" + d.appearance + " ";
        if (d.hasTxPower) details += "tx=" + String(d.txPower) + "dBm ";
        if (d.hasDistance) details += "~" + String(d.distanceM, 1) + "m ";
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

void monitorReport(uint32_t cycle, const MonitorReport& mon) {
    if (config::kJsonOutput) {
        printMonitorJson(cycle, mon);
    } else {
        printMonitorTable(mon);
    }
}

}  // namespace report
