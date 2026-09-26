#include "display.h"

#if RECON_DISPLAY

#include <Adafruit_GFX.h>
#include <Adafruit_ILI9341.h>
#include <Arduino.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <esp_memory_utils.h>

#include <algorithm>
#include <mutex>

#include "config.h"
#include "wifi_recon.h"

namespace display {
namespace {

constexpr int16_t kW = 320;
constexpr int16_t kH = 240;
constexpr int16_t kHeaderH = 22;
constexpr int16_t kRowH = 18;    // Text size 2 is 16 px tall.
constexpr int16_t kCharW = 12;   // Text size 2 is 12 px per character.

// RGB565, matching the web page's dark palette.
constexpr uint16_t kBg = 0x0882;
constexpr uint16_t kPanel = 0x10C4;
constexpr uint16_t kText = 0xE73C;
constexpr uint16_t kMuted = 0x8C94;
constexpr uint16_t kOk = 0x4E11;
constexpr uint16_t kWarn = 0xDD08;
constexpr uint16_t kBad = 0xE2A9;
constexpr uint16_t kAccent = 0x5CFB;

enum Page : uint8_t { kSummary, kWifi, kBle, kMonitor, kPageCount };
const char* const kPageTitles[kPageCount] = {"Summary", "WiFi", "Bluetooth LE", "Monitor"};

// Latest results, written by the main loop, read by the display task.
struct Snapshot {
    uint32_t version = 0;
    uint32_t cycle = 0;
    bool haveWifi = false, haveBle = false, haveMonitor = false;
    std::vector<WifiNetwork> wifi;
    std::vector<BleDevice> ble;
    MonitorReport monitor{};
};

std::mutex mu;
Snapshot latest;

Adafruit_ILI9341 tft(config::kTftCs, config::kTftDc, config::kTftReset);
XPT2046_Touchscreen touch(config::kTouchCs, config::kTouchIrq);
GFXcanvas16* canvas = nullptr;  // Full-frame buffer in PSRAM: no flicker.

// --- text helpers ------------------------------------------------------------

// The built-in font covers ASCII only; broadcast names can be any bytes.
String printable(const String& s, size_t maxChars) {
    String out;
    for (size_t i = 0; i < s.length() && out.length() < maxChars; ++i) {
        const char c = s[i];
        out += (c >= 0x20 && c <= 0x7E) ? c : '?';
    }
    return out;
}

// "aa:bb:cc:dd:ee:ff" -> "AABBCCDDEEFF": all 12 digits fit where 17 would not.
String compactAddress(const String& addr) {
    String out;
    for (size_t i = 0; i < addr.length(); ++i) {
        if (addr[i] != ':') out += static_cast<char>(toupper(addr[i]));
    }
    return out;
}

void text(GFXcanvas16& c, int16_t x, int16_t y, uint8_t size, uint16_t color, const String& s) {
    c.setTextSize(size);
    c.setTextColor(color);
    c.setCursor(x, y);
    c.print(s);
}

String fmt(const char* f, long a) {
    char buf[64];
    snprintf(buf, sizeof(buf), f, a);
    return String(buf);
}

uint16_t rssiColor(int rssi) {
    if (rssi >= -60) return kOk;
    if (rssi >= -75) return kWarn;
    return kMuted;
}

void waiting(GFXcanvas16& c) {
    text(c, 20, kH / 2 - 8, 2, kMuted, "Waiting for a sweep");
}

// --- pages -------------------------------------------------------------------

void drawHeader(GFXcanvas16& c, Page page, const Snapshot& s) {
    c.fillRect(0, 0, kW, kHeaderH, kPanel);
    text(c, 6, 3, 2, kText, kPageTitles[page]);
    if (s.cycle) text(c, 200, 7, 1, kMuted, fmt("#%ld", s.cycle));
    for (int i = 0; i < kPageCount; ++i) {  // Page indicator dots.
        const int16_t x = kW - 12 - (kPageCount - 1 - i) * 12;
        if (i == page) c.fillCircle(x, kHeaderH / 2, 4, kAccent);
        else c.drawCircle(x, kHeaderH / 2, 3, kMuted);
    }
}

void drawSummary(GFXcanvas16& c, const Snapshot& s) {
    if (!s.haveWifi && !s.haveBle && !s.haveMonitor) return waiting(c);
    int16_t y = kHeaderH + 8;
    auto line = [&](const char* label, long value, uint16_t color) {
        text(c, 8, y, 2, kText, label);
        text(c, 232, y, 2, color, fmt("%5ld", value));
        y += kRowH;
    };
    auto sub = [&](const String& t, uint16_t color) {
        text(c, 20, y, 1, color, t);
        y += 12;
    };

    if (s.haveWifi) {
        long g5 = 0, flagged = 0;
        for (const WifiNetwork& n : s.wifi) {
            if (n.channel > 14) ++g5;
            if (*wifi_recon::securityFlag(n.auth)) ++flagged;
        }
        line("WiFi networks", s.wifi.size(), kText);
        sub(fmt("2.4 GHz: %ld", s.wifi.size() - g5) + fmt("   5 GHz: %ld", g5), kMuted);
        sub(fmt("weak security: %ld", flagged), flagged ? kBad : kMuted);
    }
    if (s.haveBle) {
        long named = std::count_if(s.ble.begin(), s.ble.end(),
                                   [](const BleDevice& d) { return !d.name.isEmpty(); });
        line("BLE devices", s.ble.size(), kText);
        sub(fmt("named: %ld", named), kMuted);
    }
    if (s.haveMonitor) {
        line("Probing clients", s.monitor.clients.size(), kText);
        line("Active APs", s.monitor.aps.size(), kText);
        y += 4;
        if (s.monitor.alerts.empty()) {
            text(c, 8, y, 2, kOk, "No deauth attacks");
        } else {
            c.fillRoundRect(4, y - 3, kW - 8, kRowH + 4, 4, kBad);
            text(c, 10, y, 2, kText, fmt("DEAUTH FLOOD x%ld", s.monitor.alerts.size()));
        }
    }
    text(c, 8, kH - 10, 1, kMuted,
         fmt("up %ldm", millis() / 60000) + "   tap anywhere for the next page");
}

void drawWifi(GFXcanvas16& c, const Snapshot& s) {
    if (!s.haveWifi) return waiting(c);
    // Columns (size-2 chars): SSID 12 | CH 3 | RSSI 4 | FLAG 4.
    const int16_t xCh = 4 + 13 * kCharW, xRssi = xCh + 4 * kCharW, xFlag = xRssi + 5 * kCharW;
    constexpr size_t kRows = 10;
    int16_t y = kHeaderH + 6;
    for (size_t i = 0; i < s.wifi.size() && i < kRows; ++i, y += kRowH) {
        const WifiNetwork& n = s.wifi[i];
        const char* flag = wifi_recon::securityFlag(n.auth);
        if (n.ssid.isEmpty()) text(c, 4, y, 2, kMuted, "<hidden>");
        else text(c, 4, y, 2, *flag ? kBad : kText, printable(n.ssid, 12));
        text(c, xCh, y, 2, n.channel > 14 ? kAccent : kMuted, fmt("%3ld", n.channel));
        text(c, xRssi, y, 2, rssiColor(n.rssi), fmt("%4ld", n.rssi));
        if (*flag) text(c, xFlag, y, 2, kBad, String(flag).substring(0, 4));
    }
    if (s.wifi.size() > kRows) {
        text(c, 4, kH - 12, 1, kMuted,
             fmt("+%ld more, strongest first. Blue channel = 5 GHz.", s.wifi.size() - kRows));
    }
}

void drawBle(GFXcanvas16& c, const Snapshot& s) {
    if (!s.haveBle) return waiting(c);
    // Columns: label 16 | RSSI 4 | distance 4.
    const int16_t xRssi = 4 + 17 * kCharW, xDist = xRssi + 5 * kCharW;
    constexpr size_t kRows = 10;
    int16_t y = kHeaderH + 6;
    for (size_t i = 0; i < s.ble.size() && i < kRows; ++i, y += kRowH) {
        const BleDevice& d = s.ble[i];
        // Best available label: advertised name, decoded product, maker, address.
        if (!d.name.isEmpty()) text(c, 4, y, 2, kText, printable(d.name, 16));
        else if (!d.product.isEmpty()) text(c, 4, y, 2, kAccent, printable(d.product, 16));
        else if (!d.manufacturer.isEmpty()) text(c, 4, y, 2, kMuted, printable(d.manufacturer, 16));
        else text(c, 4, y, 2, kMuted, compactAddress(d.address));
        text(c, xRssi, y, 2, rssiColor(d.rssi), fmt("%4ld", d.rssi));
        // The path-loss estimate is meaningless far out; beyond 100 m, omit it.
        if (d.hasDistance && d.distanceM < 99.5f) {
            text(c, xDist, y, 2, kMuted, fmt("%2ldm", lroundf(d.distanceM)));
        }
    }
    if (s.ble.size() > kRows) {
        text(c, 4, kH - 12, 1, kMuted, fmt("+%ld more, strongest first", s.ble.size() - kRows));
    }
}

void drawMonitor(GFXcanvas16& c, const Snapshot& s) {
    if (!s.haveMonitor) return waiting(c);
    const MonitorReport& m = s.monitor;
    int16_t y = kHeaderH + 4;

    for (const SecurityAlert& a : m.alerts) {
        c.fillRect(0, y, kW, 12, kBad);
        text(c, 4, y + 2, 1, kText, String("DEAUTH FLOOD ") + a.bssid + fmt("  x%ld", a.count));
        y += 13;
    }

    // Packets per channel as a bar chart.
    const int16_t chartTop = y + 12, chartBottom = 138;
    if (m.channelPackets.empty()) {
        text(c, 8, chartTop + 30, 2, kMuted, "No traffic heard");
    } else {
        uint32_t peak = 1;
        for (const auto& [ch, pkts] : m.channelPackets) peak = std::max(peak, pkts);
        const int16_t slot = kW / static_cast<int16_t>(m.channelPackets.size());
        const int16_t barW = std::min<int16_t>(slot - 4, 24);
        int16_t x = (slot - barW) / 2;
        for (const auto& [ch, pkts] : m.channelPackets) {
            const int16_t h = static_cast<int16_t>((chartBottom - chartTop - 10) * pkts / peak);
            c.fillRect(x, chartBottom - h, barW, h, ch > 14 ? kAccent : kWarn);
            text(c, x, chartBottom - h - 9, 1, kMuted, fmt("%ld", pkts));
            text(c, x, chartBottom + 3, 1, kText, fmt("%ld", ch));
            x += slot;
        }
    }

    // Devices looking for networks, and the names they asked for.
    y = 156;
    text(c, 4, y, 2, kText, fmt("Probing clients %ld", m.clients.size()));
    y += kRowH + 2;
    for (size_t i = 0; i < m.clients.size() && i < 3; ++i, y += kRowH) {
        const ProbingClient& p = m.clients[i];
        const String who = p.randomized ? String("random") : printable(p.vendor.isEmpty() ? p.mac : p.vendor, 8);
        text(c, 4, y, 2, kMuted, who);
        text(c, 4 + 9 * kCharW, y, 2, kText,
             p.probedSsids.empty() ? String("(any)") : printable(p.probedSsids.front(), 12));
    }
}

void render(Page page) {
    Snapshot s;
    {
        std::lock_guard<std::mutex> lock(mu);
        s = latest;  // Copy, so drawing never holds the lock.
    }
    GFXcanvas16& c = *canvas;
    c.fillScreen(kBg);
    c.setTextWrap(false);
    drawHeader(c, page, s);
    switch (page) {
        case kSummary: drawSummary(c, s); break;
        case kWifi:    drawWifi(c, s); break;
        case kBle:     drawBle(c, s); break;
        case kMonitor: drawMonitor(c, s); break;
        default: break;
    }
    tft.drawRGBBitmap(0, 0, c.getBuffer(), kW, kH);
}

// --- task ----------------------------------------------------------------------

void task(void*) {
    uint8_t page = kSummary;
    uint32_t shownVersion = UINT32_MAX;
    uint8_t shownPage = UINT8_MAX;
    uint32_t shownMinute = UINT32_MAX;
    bool pressed = false;
    uint32_t releasedAt = 0;

    for (;;) {
        // One page per tap: advance on press, and re-arm only after the panel
        // has been released for a moment, since a press can flicker.
        const bool down = touch.touched();
        const uint32_t now = millis();
        if (down && !pressed && now - releasedAt > 150) {
            page = (page + 1) % kPageCount;
            pressed = true;
        } else if (!down && pressed) {
            pressed = false;
            releasedAt = now;
        }

        uint32_t version;
        {
            std::lock_guard<std::mutex> lock(mu);
            version = latest.version;
        }
        const uint32_t minute = now / 60000;  // Summary shows uptime in minutes.
        if (version != shownVersion || page != shownPage || minute != shownMinute) {
            render(static_cast<Page>(page));
            shownVersion = version;
            shownPage = page;
            shownMinute = minute;
        }
        vTaskDelay(pdMS_TO_TICKS(config::kTouchPollMs));
    }
}

}  // namespace

void begin() {
    canvas = new GFXcanvas16(kW, kH);
    if (canvas->getBuffer() == nullptr) {
        Serial.println("Display: no memory for the frame buffer; display disabled");
        return;
    }
    Serial.printf("Display: frame buffer in %s\n",
                  esp_ptr_external_ram(canvas->getBuffer()) ? "PSRAM" : "internal RAM");

    pinMode(config::kTftBacklight, OUTPUT);
    digitalWrite(config::kTftBacklight, HIGH);

    // Both libraries use the global SPI object; start it on our pins first.
    SPI.begin(config::kSpiSck, config::kSpiMiso, config::kSpiMosi);
    tft.begin(config::kTftSpiHz);
    tft.setRotation(config::kTftRotation);
    tft.fillScreen(kBg);
    touch.begin();
    touch.setRotation(config::kTftRotation);

    xTaskCreate(task, "display", 8192, nullptr, 1, nullptr);
}

void wifi(uint32_t cycle, const std::vector<WifiNetwork>& networks) {
    std::lock_guard<std::mutex> lock(mu);
    latest.cycle = cycle;
    latest.wifi = networks;
    latest.haveWifi = true;
    ++latest.version;
}

void ble(uint32_t cycle, const std::vector<BleDevice>& devices) {
    std::lock_guard<std::mutex> lock(mu);
    latest.cycle = cycle;
    latest.ble = devices;
    latest.haveBle = true;
    ++latest.version;
}

void monitor(uint32_t cycle, const MonitorReport& report) {
    std::lock_guard<std::mutex> lock(mu);
    latest.cycle = cycle;
    latest.monitor = report;
    latest.haveMonitor = true;
    ++latest.version;
}

}  // namespace display

#endif  // RECON_DISPLAY
