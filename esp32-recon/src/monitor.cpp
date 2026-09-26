#include "monitor.h"

#include <Arduino.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>

#include <algorithm>
#include <map>
#include <set>

#include "config.h"
#include "oui.h"

namespace monitor {
namespace {

// --- callback -> main loop hand-off ----------------------------------------
// The promiscuous callback runs in the WiFi task, concurrently with the main
// loop's dwell delay. Rather than lock shared containers, the callback does the
// minimum 802.11 header parsing and posts a fixed-size event to a queue; the
// main loop drains and aggregates it. No std, no allocation, no Serial here.

enum CapKind : uint8_t { kProbeReq, kApBeacon, kDeauth, kData };

struct CapEvent {
    uint8_t kind;
    int8_t rssi;
    uint8_t channel;
    uint8_t ap[6];
    uint8_t client[6];
    uint8_t ssidLen;
    char ssid[32];
};

QueueHandle_t queue = nullptr;

void copyMac(uint8_t dst[6], const uint8_t* src) {
    for (int i = 0; i < 6; ++i) dst[i] = src[i];
}

bool isGroupAddr(const uint8_t mac[6]) { return mac[0] & 0x01; }  // multicast/broadcast

// Finds the SSID (tagged parameter id 0) starting at `offset` within a
// management frame body. Returns copied length, 0 if absent.
uint8_t findSsid(const uint8_t* p, int len, int offset, char out[32]) {
    int i = offset;
    while (i + 2 <= len) {
        const uint8_t id = p[i];
        const uint8_t l = p[i + 1];
        if (i + 2 + l > len) break;
        if (id == 0) {
            const uint8_t n = l > 32 ? 32 : l;
            for (uint8_t k = 0; k < n; ++k) out[k] = static_cast<char>(p[i + 2 + k]);
            return n;
        }
        i += 2 + l;
    }
    return 0;
}

void IRAM_ATTR onPacket(void* buf, wifi_promiscuous_pkt_type_t type) {
    if (queue == nullptr) return;
    if (type != WIFI_PKT_MGMT && type != WIFI_PKT_DATA) return;

    const auto* pkt = static_cast<const wifi_promiscuous_pkt_t*>(buf);
    const uint8_t* p = pkt->payload;
    const int len = pkt->rx_ctrl.sig_len;
    if (len < 24) return;  // Too short to hold three MAC addresses.

    const uint8_t frameType = (p[0] >> 2) & 0x03;
    const uint8_t subtype = (p[0] >> 4) & 0x0F;
    const bool toDS = p[1] & 0x01;
    const bool fromDS = p[1] & 0x02;
    const uint8_t* a1 = p + 4;
    const uint8_t* a2 = p + 10;
    const uint8_t* a3 = p + 16;

    CapEvent ev{};
    ev.rssi = pkt->rx_ctrl.rssi;
    ev.channel = pkt->rx_ctrl.channel;

    if (frameType == 0) {  // management
        if (subtype == 4) {  // probe request
            ev.kind = kProbeReq;
            copyMac(ev.client, a2);
            ev.ssidLen = findSsid(p, len, 24, ev.ssid);  // body starts at 24
        } else if (subtype == 5 || subtype == 8) {  // probe response / beacon
            ev.kind = kApBeacon;
            copyMac(ev.ap, a3);                      // BSSID
            ev.ssidLen = findSsid(p, len, 36, ev.ssid);  // 24 + 12 fixed fields
        } else if (subtype == 10 || subtype == 12) {  // disassoc / deauth
            ev.kind = kDeauth;
            copyMac(ev.ap, a2);  // spoofed source in a flood is the AP
        } else {
            return;
        }
    } else if (frameType == 2) {  // data
        ev.kind = kData;
        if (toDS && !fromDS) {        // station -> AP
            copyMac(ev.ap, a1);
            copyMac(ev.client, a2);
        } else if (!toDS && fromDS) {  // AP -> station
            copyMac(ev.ap, a2);
            copyMac(ev.client, a1);
        } else {
            return;  // ad-hoc or WDS: no clean AP/station split.
        }
    } else {
        return;
    }

    xQueueSend(queue, &ev, 0);  // Drop on overflow rather than block the WiFi task.
}

// --- aggregation (main loop) -----------------------------------------------

String macStr(const uint8_t mac[6]) {
    char b[18];
    snprintf(b, sizeof(b), "%02X:%02X:%02X:%02X:%02X:%02X", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5]);
    return String(b);
}

struct ClientAgg {
    int rssi = -127;
    uint32_t frames = 0;
    std::set<String> ssids;
};

struct ApAgg {
    String ssid;
    std::set<String> clients;
    uint32_t frames = 0;
};

}  // namespace

void begin() {
    if (queue == nullptr) queue = xQueueCreate(config::kMonitorQueueLen, sizeof(CapEvent));
}

MonitorReport sweep() {
    MonitorReport report{};
    if (queue == nullptr) return report;

    std::map<String, ClientAgg> clients;
    std::map<String, ApAgg> aps;
    std::map<String, uint32_t> deauths;
    xQueueReset(queue);

    const wifi_promiscuous_filter_t filter = {
        .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT | WIFI_PROMIS_FILTER_MASK_DATA};
    esp_wifi_set_promiscuous(true);
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(&onPacket);

    auto drain = [&]() {
        CapEvent ev;
        while (xQueueReceive(queue, &ev, 0) == pdTRUE) {
            if (ev.channel == 0) continue;
            report.channelPackets[ev.channel]++;

            const String ssid = ev.ssidLen ? String(ev.ssid).substring(0, ev.ssidLen)
                                           : String();
            switch (ev.kind) {
                case kProbeReq: {
                    if (isGroupAddr(ev.client)) break;
                    ClientAgg& c = clients[macStr(ev.client)];
                    c.frames++;
                    c.rssi = std::max(c.rssi, static_cast<int>(ev.rssi));
                    if (ssid.length() && c.ssids.size() < config::kMaxProbesPerClient) {
                        c.ssids.insert(ssid);
                    }
                    break;
                }
                case kApBeacon: {
                    ApAgg& a = aps[macStr(ev.ap)];
                    a.frames++;
                    if (ssid.length()) a.ssid = ssid;
                    break;
                }
                case kDeauth:
                    deauths[macStr(ev.ap)]++;
                    break;
                case kData: {
                    ApAgg& a = aps[macStr(ev.ap)];
                    a.frames++;
                    if (!isGroupAddr(ev.client) &&
                        a.clients.size() < config::kMaxClientsPerAp) {
                        a.clients.insert(macStr(ev.client));
                    }
                    break;
                }
            }
        }
    };

    for (uint8_t ch : config::kMonitorChannels) {
        esp_wifi_set_channel(ch, WIFI_SECOND_CHAN_NONE);
        delay(config::kMonitorDwellMs);
        drain();
    }

    esp_wifi_set_promiscuous_rx_cb(nullptr);
    esp_wifi_set_promiscuous(false);
    drain();  // Anything queued during teardown.

    // Build the report, then sort and cap it so a busy environment can't flood
    // the output. Capping after sorting keeps the strongest clients and the
    // busiest APs, not whichever sorted first by MAC.
    report.clients.reserve(clients.size());
    for (auto& [mac, c] : clients) {
        ProbingClient pc;
        pc.mac = mac;
        pc.vendor = oui::vendorFromString(mac);
        pc.randomized = oui::randomizedFromString(mac);
        pc.rssi = c.rssi;
        pc.frames = c.frames;
        pc.probedSsids.assign(c.ssids.begin(), c.ssids.end());
        report.clients.push_back(std::move(pc));
    }
    report.aps.reserve(aps.size());
    for (auto& [bssid, a] : aps) {
        ApTraffic at;
        at.bssid = bssid;
        at.ssid = a.ssid;
        at.vendor = oui::vendorFromString(bssid);
        at.clientCount = static_cast<int>(a.clients.size());
        at.frames = a.frames;
        report.aps.push_back(std::move(at));
    }
    for (auto& [bssid, count] : deauths) {
        if (count < config::kDeauthAlertThreshold) continue;
        report.alerts.push_back({bssid, "deauth-flood", count});
    }

    std::sort(report.clients.begin(), report.clients.end(),
              [](const ProbingClient& a, const ProbingClient& b) { return a.rssi > b.rssi; });
    std::sort(report.aps.begin(), report.aps.end(),
              [](const ApTraffic& a, const ApTraffic& b) {
                  if (a.clientCount != b.clientCount) return a.clientCount > b.clientCount;
                  return a.frames > b.frames;
              });
    if (report.clients.size() > config::kMaxMonitorClients) {
        report.clients.resize(config::kMaxMonitorClients);
    }
    if (report.aps.size() > config::kMaxMonitorAps) report.aps.resize(config::kMaxMonitorAps);
    return report;
}

}  // namespace monitor
