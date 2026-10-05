// OBD-II over CAN (ISO 15765-4, 11-bit addressing): services 01, 03, 07, 09.
#pragma once

#include <stddef.h>
#include <stdint.h>

namespace obd {

// One reassembled ISO-TP reply from one ECU.
struct Response {
    uint32_t ecuId = 0;  // 0x7E8-0x7EF, or 0x18DAF1xx with 29-bit addressing
    uint8_t data[255] = {};
    size_t len = 0;
    bool negative = false;  // ECU answered 0x7F (service not supported, etc.)
};

// Sends `req` to all ECUs and collects replies whose first byte is the
// positive response to req[0] (req[0] + 0x40). With firstOnly, returns as soon
// as one ECU has answered. Returns the number of responses written to `out`.
size_t request(const uint8_t* req, size_t reqLen, Response* out, size_t maxOut, bool firstOnly);

// --- Service 01: live data ---------------------------------------------------

struct PidInfo {
    uint8_t pid;
    const char* name;
    const char* unit;
    uint8_t bytes;     // data bytes in the reply
    uint8_t decimals;  // for display
    float (*decode)(const uint8_t* d);
};

// Every PID the live screen knows how to show, in display order.
const PidInfo* livePids(size_t& count);

// Queries PIDs 00/20/40/60 and remembers which PIDs the car supports.
// Returns the number of ECUs that answered (0 = no OBD on this bus).
size_t discover();
bool isSupported(uint8_t pid);
// Distinct ECUs seen by the last discover().
size_t ecuCount();

// Reads one PID. Returns false on timeout or a short reply.
bool readPid(const PidInfo& info, float& value);

// --- Service 01 PID 01, 03 and 07: trouble codes ----------------------------

struct Dtc {
    char code[6];  // "P0301"
    uint32_t ecuId;
    bool pending;  // service 07 (pending) rather than 03 (stored)
};

struct MilStatus {
    bool valid = false;
    bool milOn = false;
    uint8_t storedCount = 0;
};

MilStatus readMilStatus();
// Appends stored (03) then pending (07) codes. Returns the number written.
size_t readDtcs(Dtc* out, size_t maxOut);

// --- Service 09: vehicle info ------------------------------------------------

// Reads the 17-character VIN into `out` (needs 18 bytes). False if unavailable.
bool readVin(char* out, size_t outLen);

}  // namespace obd
