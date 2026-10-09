// The connection to the car. The OBD layer and the sniffer talk to whichever
// link is active and never care whether frames come from our own CAN
// transceiver or from an ELM327 dongle over WiFi.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "can_frame.h"
#include "settings.h"

namespace obdlink {

enum class Next : uint8_t {
    Frame,    // `frame` holds a received frame
    Timeout,  // nothing yet; keep waiting
    End,      // the link knows the response is over (ELM printed its prompt)
};

class Link {
public:
    virtual ~Link() = default;

    // Brings the link up: joins WiFi and initialises the ELM, or starts the
    // CAN controller and detects the bitrate. Slow; `status` gets progress
    // text for the screen. Returns false with a reason in lastError().
    virtual bool connect(void (*status)(const char* line)) = 0;
    virtual void disconnect() = 0;
    virtual bool connected() = 0;
    // Call every loop.
    virtual void service() = 0;

    // --- OBD request / response ---
    // Sends one OBD request (service byte + parameters) to all ECUs.
    // firstOnly hints that one reply is enough.
    virtual bool beginRequest(const uint8_t* req, size_t len, bool firstOnly) = 0;
    // Reply frames, with ISO-TP PCI bytes intact, ID = the ECU's reply ID.
    virtual Next nextFrame(CanFrame& frame, uint32_t timeoutMs) = 0;
    // Direct CAN must send ISO-TP flow control itself; an ELM does it for us.
    virtual void sendFlowControl(uint32_t ecuReplyId) = 0;
    // Upper bound on one complete request/response.
    virtual uint32_t responseTimeoutMs() const = 0;

    // --- Passive bus monitor (sniffer) ---
    virtual bool startMonitor() = 0;
    virtual void stopMonitor() = 0;
    // Non-blocking; false when nothing is waiting.
    virtual bool readMonitor(CanFrame& frame) = 0;
    // Frames the link had to drop (queue full, ELM "BUFFER FULL").
    virtual uint32_t monitorDropped() = 0;
    // True when the link can see every frame on the bus. An ELM327 over
    // WiFi cannot keep up with a busy bus and will drop frames.
    virtual bool monitorIsLossless() const = 0;

    // Battery voltage at the OBD port, or a negative value if unknown.
    virtual float batteryVolts() = 0;
    // Short name for the header, e.g. "ELM" or "CAN".
    virtual const char* shortName() const = 0;
    // Human-readable details for the info screen (several lines, '\n'-separated).
    virtual void describe(char* out, size_t len) = 0;
    virtual const char* lastError() const = 0;
    // Increments on every frame received; drives the activity LED.
    virtual uint32_t frameCount() const = 0;
};

// The link chosen in settings. Switching kinds disconnects the old one.
Link& active();
void select(settings::LinkKind kind);

}  // namespace obdlink
