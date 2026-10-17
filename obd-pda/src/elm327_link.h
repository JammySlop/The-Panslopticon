// Link through a generic ELM327 WiFi dongle (the cheap "WiFi_OBDII" kind).
//
// We join the dongle's access point, open its TCP port and drive it with AT
// commands. With headers on (ATH1) and spaces on (ATS1) the ELM prints every
// CAN frame it receives as "7E8 10 14 49 02 01 31 44 34", PCI byte included,
// so the OBD layer reassembles replies exactly as it does for direct CAN. The
// ELM sends ISO-TP flow control itself.
#pragma once

#include <WiFi.h>

#include "link.h"

namespace obdlink {

class Elm327Link : public Link {
public:
    bool connect(void (*status)(const char* line)) override;
    void disconnect() override;
    bool connected() override;
    void service() override;

    bool beginRequest(const uint8_t* req, size_t len, bool firstOnly) override;
    Next nextFrame(CanFrame& frame, uint32_t timeoutMs) override;
    void sendFlowControl(uint32_t) override {}  // the ELM handles it
    uint32_t responseTimeoutMs() const override;

    bool startMonitor() override;
    void stopMonitor() override;
    bool readMonitor(CanFrame& frame) override;
    uint32_t monitorDropped() override { return dropped_; }
    bool monitorIsLossless() const override { return false; }

    float batteryVolts() override { return volts_; }
    const char* shortName() const override { return "ELM"; }
    void describe(char* out, size_t len) override;
    const char* lastError() const override { return error_; }
    uint32_t frameCount() const override { return frames_; }

private:
    enum class Read : int8_t { Prompt = -1, Timeout = 0, Line = 1 };

    bool joinWifi(void (*status)(const char*));
    bool openSocket(void (*status)(const char*));
    bool initElm(void (*status)(const char*));

    void writeLine(const char* text);
    void drainInput();
    // Collects everything up to the '>' prompt. False on timeout.
    bool readUntilPrompt(char* out, size_t len, uint32_t timeoutMs);
    // Sends an AT/OBD command and waits for the prompt.
    bool command(const char* cmd, char* out, size_t len, uint32_t timeoutMs);
    bool commandOk(const char* cmd);
    // Reads one line into line_. Non-blocking with timeoutMs = 0.
    Read readLine(uint32_t timeoutMs);
    // Waits out a reply we stopped reading early, so the next command does
    // not interrupt the ELM mid-response.
    void finishPending();
    bool parseFrame(const char* text, CanFrame& frame) const;
    bool is29Bit() const;
    void pollVolts();

    WiFiClient client_;
    bool connected_ = false;
    bool inRequest_ = false;
    bool monitoring_ = false;
    char line_[128] = {};
    size_t lineLen_ = 0;
    char version_[24] = "";
    uint8_t protocol_ = 0;  // ATDPN number: 6-9 = ISO 15765-4 CAN
    IPAddress host_;
    uint16_t port_ = 0;
    float volts_ = -1.0f;
    uint32_t lastVoltsPoll_ = 0;
    uint32_t frames_ = 0;
    uint32_t dropped_ = 0;
    const char* error_ = "";
};

}  // namespace obdlink
