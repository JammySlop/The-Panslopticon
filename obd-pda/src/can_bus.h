// Thin wrapper over the ESP32-C3's TWAI (CAN 2.0) controller.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace can_bus {

enum class Mode : uint8_t {
    ListenOnly,  // receive only; never transmits, not even ACK bits
    Normal,      // needed to send OBD requests
};

struct Frame {
    uint32_t id = 0;
    bool extended = false;
    bool rtr = false;
    uint8_t dlc = 0;
    uint8_t data[8] = {};
};

struct Stats {
    uint32_t rxFrames = 0;
    uint32_t txFrames = 0;
    uint32_t rxMissed = 0;   // dropped because the receive queue was full
    uint32_t busErrors = 0;
    uint32_t txErrorCounter = 0;
    uint32_t rxErrorCounter = 0;
    const char* state = "off";
};

// Starts (or restarts) the controller. Returns false if the driver refused.
bool begin(uint32_t bitrate, Mode mode);
void end();

bool running();
uint32_t bitrate();
Mode mode();

// Brings the bus up in `mode` at `bitrate`, restarting only if something changed.
bool ensure(uint32_t bitrate, Mode mode);

bool receive(Frame& frame, uint32_t timeoutMs);
// Fails immediately in listen-only mode.
bool send(const Frame& frame, uint32_t timeoutMs);

// Discards anything waiting in the receive queue.
void flush();

// Call regularly: restarts the controller after a bus-off.
void service();

Stats stats();

// Finds the bus bitrate. Listens passively at each candidate first; only if
// the bus is silent at all of them does it send an OBD "supported PIDs"
// request at each rate. Returns 0 if nothing answered. Leaves the bus stopped.
uint32_t autodetect();

}  // namespace can_bus
