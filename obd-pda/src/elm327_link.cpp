#include "elm327_link.h"

#include <Arduino.h>
#include <ctype.h>
#include <string.h>

#include "config.h"

namespace obdlink {
namespace {

bool isHex(const char* s, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!isxdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return n > 0;
}

const char* protocolName(uint8_t p) {
    switch (p) {
        case 1: return "SAE J1850 PWM";
        case 2: return "SAE J1850 VPW";
        case 3: return "ISO 9141-2";
        case 4: return "ISO 14230-4 KWP (5 baud)";
        case 5: return "ISO 14230-4 KWP (fast)";
        case 6: return "CAN 11-bit 500k";
        case 7: return "CAN 29-bit 500k";
        case 8: return "CAN 11-bit 250k";
        case 9: return "CAN 29-bit 250k";
        case 10: return "SAE J1939 CAN";
        default: return "unknown";
    }
}

}  // namespace

// --- Connection ----------------------------------------------------------------

bool Elm327Link::connect(void (*status)(const char* line)) {
    disconnect();
    if (!joinWifi(status) || !openSocket(status) || !initElm(status)) {
        const char* reason = error_;
        disconnect();
        error_ = reason;
        return false;
    }
    connected_ = true;
    error_ = "";
    return true;
}

bool Elm327Link::joinWifi(void (*status)(const char*)) {
    const settings::Settings& s = settings::get();
    char msg[64];
    snprintf(msg, sizeof(msg), "Joining %s...", s.wifiSsid);
    status(msg);

    WiFi.mode(WIFI_STA);
    // Power saving adds up to ~100 ms to every round trip; we are on USB power.
    WiFi.setSleep(false);
    WiFi.begin(s.wifiSsid, s.wifiPass[0] ? s.wifiPass : nullptr);
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED) {
        if (millis() - start > config::kWifiConnectTimeoutMs) {
            error_ = WiFi.status() == WL_NO_SSID_AVAIL ? "Dongle WiFi not found" : "Could not join dongle WiFi";
            return false;
        }
        delay(50);
    }
    return true;
}

bool Elm327Link::openSocket(void (*status)(const char*)) {
    const settings::Settings& s = settings::get();
    // The dongle is the access point, so the gateway DHCP gave us is the
    // dongle. That covers clones that don't use 192.168.0.10.
    if (!(s.elmHost[0] && host_.fromString(s.elmHost))) {
        host_ = WiFi.gatewayIP();
        if (host_ == IPAddress(0, 0, 0, 0)) host_.fromString(config::kElmFallbackHost);
    }

    uint16_t ports[sizeof(config::kElmPorts) / sizeof(config::kElmPorts[0])];
    size_t portCount = 0;
    if (s.elmPort) {
        ports[portCount++] = s.elmPort;
    } else {
        for (uint16_t p : config::kElmPorts) ports[portCount++] = p;
    }

    char msg[48];
    for (size_t i = 0; i < portCount; ++i) {
        snprintf(msg, sizeof(msg), "Connecting %s:%u...", host_.toString().c_str(), ports[i]);
        status(msg);
        if (client_.connect(host_, ports[i], config::kElmTcpConnectTimeoutMs)) {
            client_.setNoDelay(true);
            port_ = ports[i];
            return true;
        }
    }
    error_ = "Dongle not answering on TCP";
    return false;
}

bool Elm327Link::initElm(void (*status)(const char*)) {
    status("Resetting ELM327...");
    char resp[96];
    // Some clones ignore the first command after the socket opens; a bare CR
    // clears any half-typed input.
    writeLine("");
    readUntilPrompt(nullptr, 0, 300);
    if (!command("ATZ", resp, sizeof(resp), config::kElmResetTimeoutMs) &&
        !command("ATWS", resp, sizeof(resp), config::kElmResetTimeoutMs)) {
        error_ = "No ELM327 prompt";
        return false;
    }
    // The banner reads "ELM327 v1.5" (clones claim anything from v1.5 to v2.3).
    const char* banner = strstr(resp, "ELM");
    strlcpy(version_, banner ? banner : "ELM327 (no banner)", sizeof(version_));
    for (char* p = version_; *p; ++p) {
        if (*p == '\n' || *p == '\r') *p = '\0';
    }

    // Echo off, no linefeeds, spaces on, headers on, adaptive timing, CAN
    // auto-formatting on (the ELM adds PCI bytes and does flow control).
    static const char* const kInit[] = {"ATE0", "ATL0", "ATS1", "ATH1", "ATAT1", "ATCAF1", "ATSP0"};
    for (const char* cmd : kInit) {
        if (!commandOk(cmd)) {
            static char err[32];
            snprintf(err, sizeof(err), "ELM rejected %s", cmd);
            error_ = err;
            return false;
        }
    }

    // With ATSP0 the first request makes the ELM try each protocol in turn.
    status("Searching for car protocol...");
    if (!command("0100", resp, sizeof(resp), config::kElmSearchTimeoutMs)) {
        error_ = "ELM timed out searching";
        return false;
    }
    if (strstr(resp, "UNABLE") || strstr(resp, "NO DATA") || strstr(resp, "ERROR")) {
        error_ = "Car not answering (ignition on?)";
        return false;
    }

    if (!command("ATDPN", resp, sizeof(resp), config::kElmCommandTimeoutMs)) {
        error_ = "ELM did not report protocol";
        return false;
    }
    // "A6" = automatically chose protocol 6. Take the last hex digit.
    protocol_ = 0;
    for (const char* p = resp; *p; ++p) {
        const char c = static_cast<char>(toupper(static_cast<unsigned char>(*p)));
        if (c >= '0' && c <= '9') protocol_ = c - '0';
        else if (c >= 'A' && c <= 'F') protocol_ = c - 'A' + 10;
    }
    if (protocol_ < 6 || protocol_ > 9) {
        static char err[48];
        snprintf(err, sizeof(err), "%s: not supported yet", protocolName(protocol_));
        error_ = err;
        return false;
    }
    lastVoltsPoll_ = 0;
    pollVolts();
    return true;
}

void Elm327Link::disconnect() {
    if (monitoring_) stopMonitor();
    client_.stop();
    if (WiFi.getMode() != WIFI_OFF) {
        WiFi.disconnect(true);
        WiFi.mode(WIFI_OFF);
    }
    connected_ = false;
    inRequest_ = false;
    monitoring_ = false;
    lineLen_ = 0;
    volts_ = -1.0f;
}

bool Elm327Link::connected() {
    return connected_ && client_.connected() && WiFi.status() == WL_CONNECTED;
}

void Elm327Link::service() {
    if (!connected_) return;
    if (!client_.connected() || WiFi.status() != WL_CONNECTED) {
        connected_ = false;
        error_ = "Lost connection to dongle";
        return;
    }
    if (!monitoring_ && !inRequest_) pollVolts();
}

void Elm327Link::pollVolts() {
    if (lastVoltsPoll_ != 0 && millis() - lastVoltsPoll_ < config::kElmVoltsPollMs) return;
    lastVoltsPoll_ = millis();
    char resp[24];
    // ATRV reads the dongle's own supply, i.e. OBD pin 16: "12.6V".
    if (command("ATRV", resp, sizeof(resp), config::kElmCommandTimeoutMs)) {
        const float v = strtof(resp, nullptr);
        if (v > 1.0f && v < 40.0f) volts_ = v;
    }
}

void Elm327Link::describe(char* out, size_t len) {
    snprintf(out, len, "%s over WiFi\n%s\n%s:%u  %d dBm", version_, protocolName(protocol_),
             host_.toString().c_str(), port_, static_cast<int>(WiFi.RSSI()));
}

// --- Low-level I/O -----------------------------------------------------------------

void Elm327Link::writeLine(const char* text) {
    client_.print(text);
    client_.print('\r');
}

void Elm327Link::drainInput() {
    while (client_.available()) client_.read();
    lineLen_ = 0;
}

bool Elm327Link::readUntilPrompt(char* out, size_t len, uint32_t timeoutMs) {
    size_t n = 0;
    const uint32_t start = millis();
    while (millis() - start < timeoutMs) {
        if (!client_.available()) {
            if (!client_.connected()) break;
            delay(1);
            continue;
        }
        const int c = client_.read();
        if (c == '>') {
            if (out && len) out[n] = '\0';
            return true;
        }
        if (c <= 0 || !out || n + 1 >= len) continue;
        out[n++] = (c == '\r') ? '\n' : static_cast<char>(c);
    }
    if (out && len) out[n] = '\0';
    return false;
}

bool Elm327Link::command(const char* cmd, char* out, size_t len, uint32_t timeoutMs) {
    finishPending();
    drainInput();
    writeLine(cmd);
    return readUntilPrompt(out, len, timeoutMs);
}

bool Elm327Link::commandOk(const char* cmd) {
    char resp[32];
    return command(cmd, resp, sizeof(resp), config::kElmCommandTimeoutMs) && strstr(resp, "OK");
}

Elm327Link::Read Elm327Link::readLine(uint32_t timeoutMs) {
    const uint32_t start = millis();
    for (;;) {
        while (client_.available()) {
            const int c = client_.read();
            if (c == '>') {
                lineLen_ = 0;
                return Read::Prompt;
            }
            if (c == '\r' || c == '\n') {
                if (lineLen_ == 0) continue;  // blank line
                line_[lineLen_] = '\0';
                lineLen_ = 0;
                return Read::Line;
            }
            if (c > 0 && lineLen_ + 1 < sizeof(line_)) line_[lineLen_++] = static_cast<char>(c);
        }
        if (millis() - start >= timeoutMs) return Read::Timeout;
        delay(1);
    }
}

void Elm327Link::finishPending() {
    if (!inRequest_) return;
    readUntilPrompt(nullptr, 0, responseTimeoutMs());
    inRequest_ = false;
}

bool Elm327Link::is29Bit() const { return protocol_ == 7 || protocol_ == 9; }

// "7E8 03 41 0C 1A" (11-bit) or "18 DA F1 10 03 41 0C 1A" (29-bit).
bool Elm327Link::parseFrame(const char* text, CanFrame& frame) const {
    if (strchr(text, '<')) return false;  // "<RX ERROR", "<DATA ERROR": corrupt
    const char* tokens[13];
    size_t lens[13];
    size_t count = 0;
    for (const char* p = text; *p && count < 13;) {
        while (*p == ' ') ++p;
        if (!*p) break;
        const char* begin = p;
        while (*p && *p != ' ') ++p;
        tokens[count] = begin;
        lens[count] = p - begin;
        if (!isHex(begin, lens[count])) return false;  // a status message, not a frame
        ++count;
    }
    if (count == 0) return false;

    frame = CanFrame();
    if (count == 1) {
        // Clones that ignore ATS1 print "7E8064100BE3FB813": split it ourselves.
        const size_t idLen = is29Bit() ? 8 : 3;
        const size_t dataLen = lens[0] - idLen;
        if (lens[0] < idLen + 2 || dataLen % 2 != 0 || dataLen > 16) return false;
        char part[9] = {};
        memcpy(part, tokens[0], idLen);
        frame.id = strtoul(part, nullptr, 16);
        frame.extended = is29Bit();
        for (size_t i = 0; i < dataLen / 2; ++i) {
            memcpy(part, tokens[0] + idLen + i * 2, 2);
            part[2] = '\0';
            frame.data[frame.dlc++] = static_cast<uint8_t>(strtoul(part, nullptr, 16));
        }
        return true;
    }
    size_t first = 0;
    if (lens[0] == 3) {
        frame.id = strtoul(tokens[0], nullptr, 16);
        first = 1;
    } else if (lens[0] == 2 && is29Bit() && count >= 4) {
        for (size_t i = 0; i < 4; ++i) frame.id = (frame.id << 8) | strtoul(tokens[i], nullptr, 16);
        frame.extended = true;
        first = 4;
    } else {
        return false;
    }
    for (size_t i = first; i < count; ++i) {
        if (lens[i] != 2 || frame.dlc >= 8) return false;
        frame.data[frame.dlc++] = static_cast<uint8_t>(strtoul(tokens[i], nullptr, 16));
    }
    return true;
}

// --- OBD requests --------------------------------------------------------------------

uint32_t Elm327Link::responseTimeoutMs() const { return config::kElmResponseTimeoutMs; }

bool Elm327Link::beginRequest(const uint8_t* req, size_t len, bool firstOnly) {
    if (!connected() || len == 0 || len > 7) return false;
    if (monitoring_) stopMonitor();
    finishPending();
    drainInput();

    char hex[20];
    size_t n = 0;
    for (size_t i = 0; i < len; ++i) n += snprintf(hex + n, sizeof(hex) - n, "%02X", req[i]);
    // Service 01 replies are always single frames, so "one response" is safe.
    if (firstOnly && config::kElmUseResponseCount && req[0] == 0x01) {
        snprintf(hex + n, sizeof(hex) - n, "1");
    }
    writeLine(hex);
    inRequest_ = true;
    return true;
}

Next Elm327Link::nextFrame(CanFrame& frame, uint32_t timeoutMs) {
    if (!inRequest_) return Next::End;
    switch (readLine(timeoutMs)) {
        case Read::Prompt:
            inRequest_ = false;
            return Next::End;
        case Read::Timeout:
            return Next::Timeout;
        case Read::Line:
            break;
    }
    // "NO DATA", "SEARCHING...", "CAN ERROR" etc. are not frames; skip them.
    if (!parseFrame(line_, frame)) return Next::Timeout;
    ++frames_;
    return Next::Frame;
}

// --- Monitor -----------------------------------------------------------------------

bool Elm327Link::startMonitor() {
    if (!connected()) return false;
    if (monitoring_) return true;
    // Raw frames (no CAN auto-formatting), and silent monitoring so the ELM
    // does not acknowledge frames (ATCSM needs v1.4b+; clones may say "?").
    if (!commandOk("ATCAF0")) return false;
    commandOk("ATCSM1");
    drainInput();
    writeLine("ATMA");
    monitoring_ = true;
    return true;
}

void Elm327Link::stopMonitor() {
    if (!monitoring_) return;
    // Any character stops ATMA; the ELM prints "STOPPED" and a prompt.
    client_.print('\r');
    readUntilPrompt(nullptr, 0, config::kElmCommandTimeoutMs);
    monitoring_ = false;
    lineLen_ = 0;
    commandOk("ATCAF1");
}

bool Elm327Link::readMonitor(CanFrame& frame) {
    if (!monitoring_) return false;
    for (;;) {
        const Read r = readLine(0);
        if (r == Read::Timeout) return false;
        if (r == Read::Prompt) {
            // ATMA ended by itself, nearly always after "BUFFER FULL": the
            // ELM's UART to the WiFi module could not keep up. Restart it.
            writeLine("ATMA");
            continue;
        }
        if (strstr(line_, "BUFFER FULL")) {
            ++dropped_;
            continue;
        }
        if (parseFrame(line_, frame)) {
            ++frames_;
            return true;
        }
    }
}

}  // namespace obdlink
