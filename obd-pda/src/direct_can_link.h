// Link over our own CAN transceiver and the C3's TWAI controller.
#pragma once

#include "link.h"

namespace obdlink {

class DirectCanLink : public Link {
public:
    bool connect(void (*status)(const char* line)) override;
    void disconnect() override;
    bool connected() override;
    void service() override;

    bool beginRequest(const uint8_t* req, size_t len, bool firstOnly) override;
    Next nextFrame(CanFrame& frame, uint32_t timeoutMs) override;
    void sendFlowControl(uint32_t ecuReplyId) override;
    uint32_t responseTimeoutMs() const override;

    bool startMonitor() override;
    void stopMonitor() override;
    bool readMonitor(CanFrame& frame) override;
    uint32_t monitorDropped() override;
    bool monitorIsLossless() const override { return true; }

    float batteryVolts() override;
    const char* shortName() const override { return "CAN"; }
    void describe(char* out, size_t len) override;
    const char* lastError() const override { return error_; }
    uint32_t frameCount() const override;

private:
    uint32_t rate_ = 0;  // bitrate in use; 0 until connected
    const char* error_ = "";
};

}  // namespace obdlink
