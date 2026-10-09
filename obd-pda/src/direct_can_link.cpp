#include "direct_can_link.h"

#include <Arduino.h>
#include <string.h>

#include "can_bus.h"
#include "config.h"
#include "vbat.h"

namespace obdlink {

bool DirectCanLink::connect(void (*status)(const char* line)) {
    uint32_t rate = settings::get().canBitrate;
    if (rate == 0) {
        status("Detecting CAN bitrate...");
        rate = can_bus::autodetect();
        if (rate == 0) {
            error_ = "No CAN traffic found";
            return false;
        }
    }
    if (!can_bus::begin(rate, can_bus::Mode::Normal)) {
        error_ = "CAN driver failed to start";
        return false;
    }
    rate_ = rate;
    return true;
}

void DirectCanLink::disconnect() {
    can_bus::end();
    rate_ = 0;
}

bool DirectCanLink::connected() { return rate_ != 0 && can_bus::running(); }

void DirectCanLink::service() { can_bus::service(); }

bool DirectCanLink::beginRequest(const uint8_t* req, size_t len, bool) {
    if (!connected() || len == 0 || len > 7) return false;
    if (!can_bus::ensure(rate_, can_bus::Mode::Normal)) return false;
    CanFrame frame;
    frame.id = config::kObdBroadcastId;
    frame.dlc = 8;  // ISO 15765-4 requires padded 8-byte frames
    frame.data[0] = static_cast<uint8_t>(len);
    memcpy(&frame.data[1], req, len);
    can_bus::flush();
    return can_bus::send(frame, 20);
}

Next DirectCanLink::nextFrame(CanFrame& frame, uint32_t timeoutMs) {
    return can_bus::receive(frame, timeoutMs) ? Next::Frame : Next::Timeout;
}

void DirectCanLink::sendFlowControl(uint32_t ecuReplyId) {
    CanFrame fc;
    fc.id = ecuReplyId - 8;  // each ECU listens on its reply ID minus 8
    fc.dlc = 8;
    fc.data[0] = 0x30;  // flow control: continue to send
    fc.data[1] = 0x00;  // block size 0: send everything without waiting
    fc.data[2] = 0x00;  // STmin 0 ms: as fast as the ECU likes
    can_bus::send(fc, 20);
}

uint32_t DirectCanLink::responseTimeoutMs() const { return config::kObdResponseTimeoutMs; }

bool DirectCanLink::startMonitor() {
    if (!connected()) return false;
    // Listen-only: the controller does not even acknowledge frames.
    if (!can_bus::ensure(rate_, can_bus::Mode::ListenOnly)) return false;
    can_bus::flush();
    return true;
}

void DirectCanLink::stopMonitor() {}

bool DirectCanLink::readMonitor(CanFrame& frame) { return can_bus::receive(frame, 0); }

uint32_t DirectCanLink::monitorDropped() { return can_bus::stats().rxMissed; }

float DirectCanLink::batteryVolts() {
    const float v = vbat::volts();
    return v > 0 ? v : -1.0f;
}

void DirectCanLink::describe(char* out, size_t len) {
    const can_bus::Stats st = can_bus::stats();
    snprintf(out, len, "Direct CAN, %lu kbit/s %s\nBus state: %s",
             static_cast<unsigned long>(rate_ / 1000),
             settings::get().canBitrate == 0 ? "(detected)" : "(fixed)", st.state);
}

uint32_t DirectCanLink::frameCount() const { return can_bus::stats().rxFrames; }

}  // namespace obdlink
