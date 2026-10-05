#include "obd.h"

#include <Arduino.h>
#include <string.h>

#include "config.h"
#include "link.h"

namespace obd {
namespace {

// Up to 8 emission ECUs may answer a broadcast request.
constexpr size_t kMaxEcus = 8;
constexpr uint8_t kNegativeResponse = 0x7F;

// Per-ECU ISO-TP reassembly state.
struct Assembly {
    bool active = false;
    bool done = false;
    size_t expected = 0;
    uint8_t nextSeq = 0;
};

uint32_t gSupported[8] = {};  // bit i of gSupported[w] = PID (w*32 + i + 1)
size_t gEcuCount = 0;

// Decoders, formulas from SAE J1979 / the Wikipedia "OBD-II PIDs" table.
float pct(const uint8_t* d) { return d[0] * 100.0f / 255.0f; }
float temp(const uint8_t* d) { return d[0] - 40.0f; }
float raw8(const uint8_t* d) { return d[0]; }
float raw16(const uint8_t* d) { return (d[0] << 8) | d[1]; }
float trim(const uint8_t* d) { return (d[0] - 128) * 100.0f / 128.0f; }
float rpm(const uint8_t* d) { return raw16(d) / 4.0f; }
float advance(const uint8_t* d) { return d[0] / 2.0f - 64.0f; }
float maf(const uint8_t* d) { return raw16(d) / 100.0f; }
float volts(const uint8_t* d) { return raw16(d) / 1000.0f; }
float fuelRate(const uint8_t* d) { return raw16(d) / 20.0f; }

constexpr PidInfo kLivePids[] = {
    {0x0C, "RPM", "rpm", 2, 0, rpm},
    {0x0D, "Speed", "km/h", 1, 0, raw8},
    {0x05, "Coolant", "C", 1, 0, temp},
    {0x42, "ECU volts", "V", 2, 2, volts},
    {0x04, "Load", "%", 1, 0, pct},
    {0x11, "Throttle", "%", 1, 0, pct},
    {0x0F, "Intake air", "C", 1, 0, temp},
    {0x0B, "MAP", "kPa", 1, 0, raw8},
    {0x10, "MAF", "g/s", 2, 1, maf},
    {0x0E, "Timing", "deg", 1, 1, advance},
    {0x06, "STFT B1", "%", 1, 1, trim},
    {0x07, "LTFT B1", "%", 1, 1, trim},
    {0x2F, "Fuel level", "%", 1, 0, pct},
    {0x5C, "Oil temp", "C", 1, 0, temp},
    {0x46, "Ambient", "C", 1, 0, temp},
    {0x33, "Baro", "kPa", 1, 0, raw8},
    {0x5E, "Fuel rate", "L/h", 2, 1, fuelRate},
    {0x1F, "Run time", "s", 2, 0, raw16},
};

void formatDtc(uint8_t a, uint8_t b, char* out) {
    static const char kSystem[] = {'P', 'C', 'B', 'U'};
    static const char kHex[] = "0123456789ABCDEF";
    out[0] = kSystem[a >> 6];
    out[1] = kHex[(a >> 4) & 0x3];
    out[2] = kHex[a & 0xF];
    out[3] = kHex[b >> 4];
    out[4] = kHex[b & 0xF];
    out[5] = '\0';
}

size_t collectDtcs(uint8_t service, bool pending, Dtc* out, size_t maxOut) {
    Response resp[kMaxEcus];
    const uint8_t req[] = {service};
    const size_t n = request(req, sizeof(req), resp, kMaxEcus, false);
    size_t written = 0;
    for (size_t r = 0; r < n; ++r) {
        if (resp[r].negative || resp[r].len < 2) continue;
        // On CAN the reply is [0x40+service, count, A, B, A, B, ...].
        const size_t count = resp[r].data[1];
        for (size_t i = 0; i < count && written < maxOut; ++i) {
            const size_t at = 2 + i * 2;
            if (at + 1 >= resp[r].len) break;
            const uint8_t a = resp[r].data[at];
            const uint8_t b = resp[r].data[at + 1];
            if (a == 0 && b == 0) continue;  // padding, not a code
            formatDtc(a, b, out[written].code);
            out[written].ecuId = resp[r].ecuId;
            out[written].pending = pending;
            ++written;
        }
    }
    return written;
}

bool isObdReply(const CanFrame& f) {
    if (!f.extended) return f.id >= config::kObdReplyIdFirst && f.id <= config::kObdReplyIdLast;
    // 29-bit addressing (ISO 15765-4): ECU xx replies to the tester as 18DAF1xx.
    return (f.id & 0xFFFFFF00u) == 0x18DAF100u;
}

}  // namespace

size_t request(const uint8_t* req, size_t reqLen, Response* out, size_t maxOut, bool firstOnly) {
    if (reqLen == 0 || reqLen > 7 || maxOut == 0) return 0;
    obdlink::Link& link = obdlink::active();
    if (!link.connected() || !link.beginRequest(req, reqLen, firstOnly)) return 0;

    // One slot per replying ECU, in order of first reply; slot i fills out[i].
    struct Slot {
        uint32_t id;
        Assembly a;
    };
    Slot slots[kMaxEcus];
    const size_t maxSlots = min(maxOut, kMaxEcus);
    size_t used = 0;
    const uint8_t positive = req[0] + 0x40;
    uint32_t deadline = millis() + link.responseTimeoutMs();
    // Never shorten the deadline, only extend it while a multi-frame reply is
    // still arriving.
    auto extend = [&deadline](uint32_t ms) {
        const uint32_t until = millis() + ms;
        if (static_cast<int32_t>(until - deadline) > 0) deadline = until;
    };

    while (static_cast<int32_t>(deadline - millis()) > 0) {
        CanFrame rx;
        const obdlink::Next next = link.nextFrame(rx, 5);
        if (next == obdlink::Next::End) break;
        if (next == obdlink::Next::Timeout || !isObdReply(rx)) continue;

        size_t slot = 0;
        while (slot < used && slots[slot].id != rx.id) ++slot;
        if (slot == used) {
            if (used >= maxSlots) continue;
            slots[used].id = rx.id;
            slots[used].a = Assembly();
            out[used] = Response();
            out[used].ecuId = rx.id;
            ++used;
        }
        Assembly& a = slots[slot].a;
        Response& r = out[slot];
        if (a.done) continue;

        switch (rx.data[0] >> 4) {
            case 0x0: {  // single frame
                const size_t len = rx.data[0] & 0x0F;
                if (len == 0 || len > 7) break;
                memcpy(r.data, &rx.data[1], len);
                r.len = len;
                a.done = true;
                break;
            }
            case 0x1: {  // first frame of a multi-frame reply
                const size_t len = ((rx.data[0] & 0x0F) << 8) | rx.data[1];
                if (len < 8 || len > sizeof(r.data)) {
                    a.done = true;  // larger than we can hold; give up on it
                    break;
                }
                memcpy(r.data, &rx.data[2], 6);
                r.len = 6;
                a.expected = len;
                a.nextSeq = 1;
                a.active = true;
                link.sendFlowControl(rx.id);
                extend(config::kIsoTpFrameTimeoutMs);
                break;
            }
            case 0x2: {  // consecutive frame
                if (!a.active || (rx.data[0] & 0x0F) != a.nextSeq) break;
                const size_t take = min<size_t>(7, a.expected - r.len);
                memcpy(&r.data[r.len], &rx.data[1], take);
                r.len += take;
                a.nextSeq = (a.nextSeq + 1) & 0x0F;
                extend(config::kIsoTpFrameTimeoutMs);
                if (r.len >= a.expected) a.done = true;
                break;
            }
            default: break;  // flow control from someone else; ignore
        }

        if (!a.done) continue;
        a.active = false;
        r.negative = r.len >= 1 && r.data[0] == kNegativeResponse;
        if (!r.negative && (r.len == 0 || r.data[0] != positive)) {
            r.len = 0;  // reply to some other request; drop it
            continue;
        }
        if (firstOnly && !r.negative) break;
    }

    // Keep only finished, non-empty responses, in arrival order.
    size_t kept = 0;
    for (size_t slot = 0; slot < used; ++slot) {
        if (!slots[slot].a.done || out[slot].len == 0) continue;
        if (slot != kept) out[kept] = out[slot];
        ++kept;
    }
    return kept;
}

const PidInfo* livePids(size_t& count) {
    count = sizeof(kLivePids) / sizeof(kLivePids[0]);
    return kLivePids;
}

size_t discover() {
    memset(gSupported, 0, sizeof(gSupported));
    uint32_t ecusSeen[kMaxEcus];
    size_t seen = 0;
    for (uint8_t base = 0x00; base <= 0xC0; base += 0x20) {
        // Each range PID reports the next 32; only ask for ranges the car
        // said exist (PID 00 always does).
        if (base != 0 && !isSupported(base)) break;
        Response resp[kMaxEcus];
        const uint8_t req[] = {0x01, base};
        const size_t n = request(req, sizeof(req), resp, kMaxEcus, false);
        for (size_t r = 0; r < n; ++r) {
            if (resp[r].negative || resp[r].len < 6 || resp[r].data[1] != base) continue;
            size_t e = 0;
            while (e < seen && ecusSeen[e] != resp[r].ecuId) ++e;
            if (e == seen && seen < kMaxEcus) ecusSeen[seen++] = resp[r].ecuId;
            const uint32_t bits = (uint32_t(resp[r].data[2]) << 24) | (uint32_t(resp[r].data[3]) << 16) |
                                  (uint32_t(resp[r].data[4]) << 8) | resp[r].data[5];
            // The bitmap is MSB-first: the top bit is PID base+1. Bit-reverse
            // into our LSB-first layout and OR across ECUs.
            uint32_t reversed = 0;
            for (int i = 0; i < 32; ++i) {
                if (bits & (1u << (31 - i))) reversed |= 1u << i;
            }
            gSupported[base / 0x20] |= reversed;
        }
    }
    gEcuCount = seen;
    return gEcuCount;
}

bool isSupported(uint8_t pid) {
    if (pid == 0) return true;
    const uint8_t index = pid - 1;
    return gSupported[index / 32] & (1u << (index % 32));
}

size_t ecuCount() { return gEcuCount; }

bool readPid(const PidInfo& info, float& value) {
    Response resp;
    const uint8_t req[] = {0x01, info.pid};
    if (request(req, sizeof(req), &resp, 1, true) == 0) return false;
    if (resp.negative || resp.data[1] != info.pid || resp.len < size_t(2 + info.bytes)) return false;
    value = info.decode(&resp.data[2]);
    return true;
}

MilStatus readMilStatus() {
    MilStatus status;
    Response resp[kMaxEcus];
    const uint8_t req[] = {0x01, 0x01};
    const size_t n = request(req, sizeof(req), resp, kMaxEcus, false);
    for (size_t r = 0; r < n; ++r) {
        if (resp[r].negative || resp[r].len < 3 || resp[r].data[1] != 0x01) continue;
        status.valid = true;
        status.milOn |= (resp[r].data[2] & 0x80) != 0;
        status.storedCount += resp[r].data[2] & 0x7F;
    }
    return status;
}

size_t readDtcs(Dtc* out, size_t maxOut) {
    size_t n = collectDtcs(0x03, false, out, maxOut);
    n += collectDtcs(0x07, true, out + n, maxOut - n);
    return n;
}

bool readVin(char* out, size_t outLen) {
    if (outLen < 18) return false;
    Response resp;
    const uint8_t req[] = {0x09, 0x02};
    if (request(req, sizeof(req), &resp, 1, true) == 0 || resp.negative) return false;
    // [0x49, 0x02, item count, 17 ASCII characters]
    if (resp.len < 3 + 17 || resp.data[1] != 0x02) return false;
    const uint8_t* vin = &resp.data[resp.len - 17];
    for (int i = 0; i < 17; ++i) {
        out[i] = (vin[i] >= 0x20 && vin[i] < 0x7F) ? static_cast<char>(vin[i]) : '?';
    }
    out[17] = '\0';
    return true;
}

}  // namespace obd
